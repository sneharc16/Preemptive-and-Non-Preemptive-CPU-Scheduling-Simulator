#!/usr/bin/env python3
"""Deliberately naive tick-by-tick reference simulator.

Written independently of the C engine and shares no code with it. Time
advances one tick at a time; every tick each core either runs one process,
spends one tick of a context switch, or idles. There is no event queue and
no slice deadlines: policies that react to newly ready processes are simply
asked again EVERY tick (the running process is handed back and the choice is
remade), which must give the same schedule as the engine's event-driven
decisions. Policies use plain lists and linear scans, not heaps or trees.

It is slow on purpose; its only job is to be obviously correct so that
tests/test_differential.py can compare the C implementation against it.

A workload is a list of Proc. `simulate` returns a Result with the per-core
tick sequence, start/completion times and switch accounting.

Order of events within one tick t (the specification being tested):
  1. processes arriving at t join the policy, in pid order
  2. processes whose I/O completes at t join the policy, in pid order
  3. per core, a run slice that has used its length ends: the process
     completes, starts I/O, or is handed back to the policy
  4. per core, a context switch that has finished: the process starts running
     (it always runs at least one tick before it can be handed back)
  5. re-deciding policies get back every process that has run >= 1 tick of
     its current slice (core order)
  6. free cores ask the policy for work; a process that last ran on a free
     core goes back to that core, the rest fill free cores in index order;
     switching a core to a different process than it last ran costs cs_cost
"""
from dataclasses import dataclass, field
from fractions import Fraction

MASK64 = (1 << 64) - 1

# Linux sched_prio_to_weight[], index nice + 20
NICE_TO_WEIGHT = [
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
    9548, 7620, 6100, 4904, 3906, 3121, 2501, 1991, 1586, 1277,
    1024, 820, 655, 526, 423, 335, 272, 215, 172, 137,
    110, 87, 70, 56, 45, 36, 29, 23, 18, 15,
]


@dataclass
class Proc:
    pid: int
    arrival: int
    phases: list  # cpu, io, cpu, ..., cpu
    priority: int = 0
    tickets: int = 100
    nice: int = 0

    @property
    def cpu(self):
        return sum(self.phases[0::2])

    @property
    def io(self):
        return sum(self.phases[1::2])


def simple(pid, arrival, burst, **kw):
    return Proc(pid, arrival, [burst], **kw)


@dataclass
class Result:
    ticks: list  # per core: list of None | ("run", pid) | ("cs", pid), from first arrival
    start: dict = field(default_factory=dict)
    completion: dict = field(default_factory=dict)
    switches: int = 0
    cs_time: int = 0
    first_arrival: int = 0


# ------------------------------------------------------------------ state


class State:
    """What a policy may look at: the clock and per-process burst progress."""

    def __init__(self, procs, predict, alpha, tau0):
        self.procs = {p.pid: p for p in procs}
        self.t = 0
        self.phase = {p.pid: 0 for p in procs}
        self.remaining = {p.pid: p.phases[0] for p in procs}
        self.burst_len = {p.pid: p.phases[0] for p in procs}
        self.predict = predict
        self.alpha = alpha
        self.tau = {p.pid: tau0 for p in procs}

    def used(self, pid):
        return self.burst_len[pid] - self.remaining[pid]

    def est_burst(self, pid):
        return self.burst_len[pid] if self.predict == "oracle" else self.tau[pid]

    def est_remaining(self, pid):
        if self.predict == "oracle":
            return self.remaining[pid]
        return max(self.tau[pid] - float(self.used(pid)), 0.0)


# ---------------------------------------------------------------- policies
# Each policy: arrive(p), ready(p), pick() -> (pid, length) | None,
# preempt(p, ran), block(p, ran), complete(p, ran), and attribute `redecide`.


class Policy:
    redecide = False  # re-asked every tick

    def __init__(self, st, opts):
        self.st, self.opts = st, opts

    def ready(self, p):
        self.arrive(p)

    def block(self, p, ran):
        pass

    def complete(self, p, ran):
        pass


class Fcfs(Policy):
    def __init__(self, st, opts):
        super().__init__(st, opts)
        self.q = []

    def arrive(self, p):
        self.q.append(p)

    def pick(self):
        if not self.q:
            return None
        p = self.q.pop(0)
        return p, self.st.remaining[p]


class KeyedNonPreemptive(Policy):
    def __init__(self, st, opts):
        super().__init__(st, opts)
        self.ready_list = []

    def arrive(self, p):
        self.ready_list.append(p)

    def pick(self):
        if not self.ready_list:
            return None
        p = min(self.ready_list, key=self.key)
        self.ready_list.remove(p)
        return p, self.st.remaining[p]


class Sjf(KeyedNonPreemptive):
    def key(self, p):
        return (self.st.est_burst(p), self.st.procs[p].arrival, p)


class Srtf(KeyedNonPreemptive):
    redecide = True

    def key(self, p):
        return (self.st.est_remaining(p), self.st.procs[p].arrival, p)

    def preempt(self, p, ran):
        self.ready_list.append(p)


class Rr(Fcfs):
    def pick(self):
        if not self.q:
            return None
        return self.q.pop(0), self.opts["quantum"]

    def preempt(self, p, ran):
        self.q.append(p)


class Priority(Policy):
    """effective = priority - waited_in_ready_this_burst // aging"""

    def __init__(self, st, opts, preemptive):
        super().__init__(st, opts)
        self.redecide = preemptive
        self.ready_list = []
        self.waited = {}
        self.since = {}

    def _enter(self, p):
        self.since[p] = self.st.t
        self.ready_list.append(p)

    def arrive(self, p):
        self.waited[p] = 0
        self._enter(p)

    def preempt(self, p, ran):
        self._enter(p)

    def key(self, p):
        base = self.st.procs[p].priority
        k = self.opts["aging"]
        if k:
            base -= (self.waited[p] + self.st.t - self.since[p]) // k
        return (base, self.st.procs[p].arrival, p)

    def pick(self):
        if not self.ready_list:
            return None
        p = min(self.ready_list, key=self.key)
        self.ready_list.remove(p)
        self.waited[p] += self.st.t - self.since[p]
        return p, self.st.remaining[p]


class Hrrn(KeyedNonPreemptive):
    def __init__(self, st, opts):
        super().__init__(st, opts)
        self.since = {}

    def arrive(self, p):
        self.since[p] = self.st.t
        self.ready_list.append(p)

    def pick(self):
        if not self.ready_list:
            return None
        st = self.st

        def ratio(p):
            w = st.t - self.since[p]
            if st.predict == "oracle":
                return Fraction(w, st.burst_len[p])
            return w / st.tau[p]

        p = min(self.ready_list, key=lambda p: (-ratio(p), st.procs[p].arrival, p))
        self.ready_list.remove(p)
        return p, st.remaining[p]


class Mlfq(Policy):
    redecide = True

    def __init__(self, st, opts):
        super().__init__(st, opts)
        self.quanta = opts["mlfq_quanta"]
        self.allot = opts["mlfq_allot"] or self.quanta
        self.boost = opts["mlfq_boost"]
        self.queues = [[] for _ in self.quanta]
        self.level, self.used, self.left = {}, {}, {}
        self.epoch = st.t // self.boost if self.boost else 0
        self.dispatched_in = {}  # boost epoch in which each job was last dispatched

    def _boost(self):
        if not self.boost or self.st.t // self.boost == self.epoch:
            return
        self.epoch = self.st.t // self.boost
        merged = [p for q in self.queues for p in q]
        self.queues = [merged] + [[] for _ in self.quanta[1:]]
        for p in self.level:  # every known job, wherever it is
            self.level[p], self.used[p], self.left[p] = 0, 0, 0

    def arrive(self, p):
        self._boost()
        self.level[p], self.used[p], self.left[p] = 0, 0, 0
        self.queues[0].append(p)

    def ready(self, p):
        self._boost()
        self.left[p] = 0
        self.queues[self.level[p]].append(p)

    def _charge(self, p, ran):
        lvl = self.level[p]
        self.used[p] += ran
        if lvl + 1 < len(self.quanta) and self.used[p] >= self.allot[lvl]:
            self.level[p], self.used[p] = lvl + 1, 0
            return True
        return False

    def preempt(self, p, ran):
        self._boost()
        if self.dispatched_in[p] != self.epoch:  # a boost happened since dispatch
            self.queues[0].append(p)
            return
        self.left[p] -= ran
        if self._charge(p, ran) or self.left[p] == 0:
            self.left[p] = 0
            self.queues[self.level[p]].append(p)
        else:
            self.queues[self.level[p]].insert(0, p)

    def block(self, p, ran):
        self._boost()
        if self.dispatched_in[p] == self.epoch:
            self._charge(p, ran)
        self.left[p] = 0

    def pick(self):
        self._boost()
        for lvl, q in enumerate(self.queues):
            if q:
                p = q.pop(0)
                if self.left[p] == 0:
                    self.left[p] = self.quanta[lvl]
                length = self.left[p]
                if lvl + 1 < len(self.quanta):
                    length = min(length, self.allot[lvl] - self.used[p])
                self.left[p] = length
                self.dispatched_in[p] = self.epoch
                return p, length
        return None


def splitmix64(state):
    state = (state + 0x9E3779B97F4A7C15) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return state, z ^ (z >> 31)


class Lottery(Rr):
    def __init__(self, st, opts):
        super().__init__(st, opts)
        self.rng = opts["seed"] & MASK64

    def pick(self):
        if not self.q:
            return None
        total = sum(self.st.procs[p].tickets for p in self.q)
        threshold = (2 ** 64 - total) % total
        while True:
            self.rng, x = splitmix64(self.rng)
            if x >= threshold:
                break
        r = x % total
        for p in sorted(self.q):
            r -= self.st.procs[p].tickets
            if r < 0:
                self.q.remove(p)
                return p, self.opts["quantum"]
        raise AssertionError("unreachable")


class Stride(Policy):
    STRIDE1 = 1 << 20

    def __init__(self, st, opts):
        super().__init__(st, opts)
        self.ready_list = []
        self.passv = {}
        self.vtime = 0

    def arrive(self, p):
        self.passv[p] = self.vtime
        self.ready_list.append(p)

    def ready(self, p):
        self.passv[p] = max(self.passv[p], self.vtime)
        self.ready_list.append(p)

    def _charge(self, p, ran):
        self.passv[p] += (self.STRIDE1 // self.st.procs[p].tickets) * ran

    def preempt(self, p, ran):
        self._charge(p, ran)
        self.ready_list.append(p)

    block = complete = _charge

    def pick(self):
        if not self.ready_list:
            return None
        p = min(self.ready_list, key=lambda p: (self.passv[p], p))
        self.ready_list.remove(p)
        self.vtime = self.passv[p]
        return p, self.opts["quantum"]


class Cfs(Policy):
    def __init__(self, st, opts):
        super().__init__(st, opts)
        self.ready_list = []
        self.vr = {}
        self.min_vr = 0
        self.load = 0
        self.lat, self.gran = opts["cfs_latency"], opts["cfs_min_gran"]

    def _w(self, p):
        return NICE_TO_WEIGHT[self.st.procs[p].nice + 20]

    def _update_min(self):
        if self.ready_list:
            left = min(self.vr[p] for p in self.ready_list)
            self.min_vr = max(self.min_vr, left)

    def _push(self, p):
        self.ready_list.append(p)
        self._update_min()

    def _delta(self, p, ran):
        return (ran << 26) // self._w(p)

    def arrive(self, p):
        self.vr[p] = self.min_vr
        self.load += self._w(p)
        self._push(p)

    def ready(self, p):
        self.vr[p] = max(self.vr[p], self.min_vr - (self.lat << 16) // 2)
        self.load += self._w(p)
        self._push(p)

    def preempt(self, p, ran):
        self.vr[p] += self._delta(p, ran)
        self._push(p)

    def block(self, p, ran):
        self.vr[p] += self._delta(p, ran)
        self.load -= self._w(p)

    complete = block

    def pick(self):
        if not self.ready_list:
            return None
        p = min(self.ready_list, key=lambda p: (self.vr[p], p))
        self.ready_list.remove(p)
        self._update_min()
        return p, max(self.lat * self._w(p) // self.load, self.gran)


POLICIES = {
    "fcfs": Fcfs, "sjf": Sjf, "srtf": Srtf, "rr": Rr,
    "prio": lambda st, o: Priority(st, o, False), "prio-p": lambda st, o: Priority(st, o, True),
    "hrrn": Hrrn, "mlfq": Mlfq, "lottery": Lottery, "stride": Stride, "cfs": Cfs,
}

DEFAULTS = dict(quantum=2, aging=0, mlfq_quanta=(2, 4, 8), mlfq_allot=None, mlfq_boost=0,
                seed=1, cfs_latency=24, cfs_min_gran=3)


# ------------------------------------------------------------------ engine


def simulate(procs, algorithm, cores=1, cs_cost=0, predict="oracle", alpha=0.5, tau0=5.0,
             **options):
    opts = {**DEFAULTS, **options}
    st = State(procs, predict, alpha, tau0)
    t0 = min(p.arrival for p in procs)
    st.t = t0
    policy = POLICIES[algorithm](st, opts)
    res = Result(ticks=[[] for _ in range(cores)], first_arrival=t0)
    wake = {}
    done = 0
    # per core: dict(proc, mode 'cs'|'run', left (cs ticks or run ticks), ran, context)
    core = [dict(proc=None, mode=None, left=0, ran=0, context=None) for _ in range(cores)]

    def end_slice(c):
        nonlocal done
        p, ran = c["proc"], c["ran"]
        c["proc"] = None
        if st.remaining[p] > 0:
            policy.preempt(p, ran)
            return
        if predict == "ewma":
            st.tau[p] = alpha * float(st.burst_len[p]) + (1.0 - alpha) * st.tau[p]
        k = st.phase[p]
        phases = st.procs[p].phases
        if k + 1 == len(phases):
            res.completion[p] = st.t
            done += 1
            policy.complete(p, ran)
        else:
            wake[p] = st.t + phases[k + 1]
            st.phase[p] = k + 2
            st.remaining[p] = st.burst_len[p] = phases[k + 2]
            policy.block(p, ran)

    def start_run(c, length):
        c["mode"], c["ran"] = "run", 0
        c["left"] = min(length, st.remaining[c["proc"]])

    while done < len(procs):
        t = st.t
        for p in sorted(p.pid for p in procs if p.arrival == t):
            policy.arrive(p)
        for p in sorted(p for p, w in wake.items() if w == t):
            del wake[p]
            policy.ready(p)
        for c in core:  # 3. slices that ran their full length
            if c["proc"] is not None and c["mode"] == "run" and c["left"] == 0:
                end_slice(c)
        for c in core:  # 4. switches that finished: the process runs >= 1 tick
            if c["proc"] is not None and c["mode"] == "cs" and c["left"] == 0:
                start_run(c, c["length"])
        if policy.redecide:  # 5. hand back running processes to re-decide
            for c in core:
                if c["proc"] is not None and c["mode"] == "run" and c["ran"] > 0:
                    end_slice(c)
        if done == len(procs):
            break
        # 6. dispatch
        free = [i for i, c in enumerate(core) if c["proc"] is None]
        picks = []
        while len(picks) < len(free):
            choice = policy.pick()
            if choice is None:
                break
            picks.append(choice)
        taken = set()
        placement = []
        leftovers = []
        for p, length in picks:
            home = next((i for i in free if i not in taken and core[i]["context"] == p), None)
            if home is not None and cores > 1:
                taken.add(home)
                placement.append((home, p, length))
            else:
                leftovers.append((p, length))
        for p, length in leftovers:
            i = next(i for i in free if i not in taken)
            taken.add(i)
            placement.append((i, p, length))
        for i, p, length in placement:
            c = core[i]
            switching = c["context"] is not None and c["context"] != p
            c["proc"], c["context"], c["length"] = p, p, length
            if switching:
                res.switches += 1
            if switching and cs_cost > 0:
                c["mode"], c["left"] = "cs", cs_cost
                res.cs_time += cs_cost
            else:
                start_run(c, length)
        # 7. one tick passes
        for i, c in enumerate(core):
            if c["proc"] is None:
                res.ticks[i].append(None)
            elif c["mode"] == "cs":
                res.ticks[i].append(("cs", c["proc"]))
                c["left"] -= 1
            else:
                p = c["proc"]
                res.ticks[i].append(("run", p))
                res.start.setdefault(p, t)
                st.remaining[p] -= 1
                c["ran"] += 1
                c["left"] -= 1
        st.t += 1
    return res


def segments(res, core=0):
    """Collapses one core's ticks into [(start, end, kind, pid), ...]."""
    out = []
    for i, tick in enumerate(res.ticks[core]):
        kind, pid = ("idle", None) if tick is None else tick
        t = res.first_arrival + i
        if out and out[-1][2] == kind and out[-1][3] == pid:
            out[-1] = (out[-1][0], t + 1, kind, pid)
        else:
            out.append((t, t + 1, kind, pid))
    return out


def prediction_error(procs, alpha, tau0):
    """(bursts, MAE, MAPE%) of EWMA predictions over every CPU burst."""
    abs_sum = pct_sum = 0.0
    count = 0
    for p in procs:
        tau = tau0
        for actual in p.phases[0::2]:
            err = abs(tau - actual)
            abs_sum += err
            pct_sum += err / actual
            count += 1
            tau = alpha * float(actual) + (1.0 - alpha) * tau
    return count, abs_sum / count, 100.0 * pct_sum / count


if __name__ == "__main__":
    import sys

    rows = [tuple(int(x) for x in line.split()) for line in sys.stdin if line.strip()]
    workload = [simple(*r) for r in rows]
    q = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    for alg in POLICIES:
        print(alg, segments(simulate(workload, alg, quantum=q)))
