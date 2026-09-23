#!/usr/bin/env python3
"""Seeded synthetic workload generator; writes the simulator's CSV input.

Arrivals form a Poisson process: inter-arrival times are exponential with
rate lambda (`--rate`, or derived from a target utilisation with `--load`),
and arrival i is floor(sum of the first i inter-arrival times). CPU bursts
are drawn from one of four distributions, all with mean `--mean` (ticks,
rounded to integers >= 1):

  exp      exponential
  uniform  uniform on 1 .. 2*mean-1
  bimodal  80% interactive jobs (exponential, mean/4) and 20% batch jobs
           (exponential, 4*mean): the mixture mean is mean
  pareto   Pareto with shape --alpha (default 1.5: finite mean, infinite
           variance) and scale mean*(alpha-1)/alpha, capped at --cap*mean

With `--io-fraction F`, a fraction F of processes are I/O-bound: 3 to 7
short CPU bursts (a quarter of a regular burst each) separated by I/O phases
(exponential, mean `--io-mean`). The others are CPU-bound single bursts.
Priorities (0-9), lottery tickets and nice values are drawn uniformly.

The same arguments always produce the same file (Python's `random.Random`
is seeded explicitly and its sequence is stable across versions).

Example:  tools/gen.py --n 1000 --dist pareto --load 0.9 --seed 1 > w.csv
"""
import argparse
import random
import sys

DISTS = ("exp", "uniform", "bimodal", "pareto")


def burst_sampler(rng, dist, mean, alpha, cap):
    if dist == "exp":
        draw = lambda: rng.expovariate(1.0 / mean)
    elif dist == "uniform":
        draw = lambda: rng.randint(1, max(1, 2 * round(mean) - 1))
    elif dist == "bimodal":
        draw = lambda: (rng.expovariate(4.0 / mean) if rng.random() < 0.8
                        else rng.expovariate(1.0 / (4 * mean)))
    elif dist == "pareto":
        scale = mean * (alpha - 1) / alpha
        draw = lambda: min(scale * rng.paretovariate(alpha), cap * mean)
    else:
        raise ValueError(dist)
    return lambda: max(1, int(round(draw())))


def generate(n, seed=1, dist="exp", mean=10.0, rate=None, load=0.9, cores=1, alpha=1.5,
             cap=1000.0, io_fraction=0.0, io_mean=20.0):
    """Returns a list of dict rows (pid, arrival, bursts, priority, tickets, nice)."""
    if rate is None:
        rate = load * cores / mean
    rng = random.Random(seed)
    burst = burst_sampler(rng, dist, mean, alpha, cap)
    rows, clock = [], 0.0
    for pid in range(1, n + 1):
        if pid > 1:
            clock += rng.expovariate(rate)
        if rng.random() < io_fraction:
            phases = []
            for k in range(rng.randint(3, 7)):
                if k:
                    phases.append(max(1, int(round(rng.expovariate(1.0 / io_mean)))))
                phases.append(max(1, burst() // 4))
        else:
            phases = [burst()]
        rows.append(dict(pid=pid, arrival=int(clock), bursts=phases,
                         priority=rng.randint(0, 9), tickets=rng.choice((50, 100, 200, 400)),
                         nice=rng.randint(-20, 19)))
    return rows


def to_csv(rows):
    out = ["pid,arrival,bursts,priority,tickets,nice"]
    for r in rows:
        out.append(f"{r['pid']},{r['arrival']},{':'.join(map(str, r['bursts']))},"
                   f"{r['priority']},{r['tickets']},{r['nice']}")
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--n", type=int, required=True, help="number of processes")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--dist", choices=DISTS, default="exp")
    ap.add_argument("--mean", type=float, default=10.0, help="mean CPU burst (ticks)")
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--rate", type=float, help="arrival rate lambda (processes/tick)")
    group.add_argument("--load", type=float, default=0.9,
                       help="target utilisation; rate = load * cores / mean (default 0.9)")
    ap.add_argument("--cores", type=int, default=1, help="cores assumed by --load")
    ap.add_argument("--alpha", type=float, default=1.5, help="Pareto shape (> 1)")
    ap.add_argument("--cap", type=float, default=1000.0, help="Pareto cap, in means")
    ap.add_argument("--io-fraction", type=float, default=0.0)
    ap.add_argument("--io-mean", type=float, default=20.0)
    a = ap.parse_args(argv)
    if a.n < 1 or a.mean < 1 or a.alpha <= 1 or not 0 <= a.io_fraction <= 1:
        ap.error("need n >= 1, mean >= 1, alpha > 1 and 0 <= io-fraction <= 1")
    rows = generate(a.n, a.seed, a.dist, a.mean, a.rate, a.load, a.cores, a.alpha, a.cap,
                    a.io_fraction, a.io_mean)
    sys.stdout.write(to_csv(rows))


if __name__ == "__main__":
    main()
