#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

/*
  CPU Scheduling Algorithms: FCFS(FIFO), SJF (non-preemptive), SRTF, Round Robin
  Improvements:
  - No VLAs: uses malloc/free for portability.
  - Works with arbitrary PID values (no "PID-1" indexing).
  - Correct SJF handling when no process has arrived yet (CPU idle -> jump to next arrival).
  - SRTF picks next on ties deterministically (arrival, then PID).
  - Round Robin fairly enqueues arrivals during a running time-slice before re-adding the current process.
  - Prints per-algorithm averages (Response, Waiting, Turnaround).
  - Robust input validation and deterministic tie-breaking.
*/

typedef struct {
    int pid;
    int arrival;
    int burst;
} Proc;

/* ---------- Utility ---------- */

static int read_int(const char *label) {
    int x;
    if (scanf("%d", &x) != 1) {
        fprintf(stderr, "ERROR: Failed to read %s.\n", label);
        exit(1);
    }
    return x;
}

/* min index by arrival among !done; tie by pid */
static int pick_min_arrival(const Proc *p, int n, const int *done) {
    int idx = -1, bestA = INT_MAX, bestPid = INT_MAX;
    for (int i = 0; i < n; ++i) if (!done[i]) {
        if (p[i].arrival < bestA || (p[i].arrival == bestA && p[i].pid < bestPid)) {
            bestA = p[i].arrival; bestPid = p[i].pid; idx = i;
        }
    }
    return idx;
}

/* min burst among arrived (arrival <= t) and !done; tie by arrival then pid */
static int pick_sjf(const Proc *p, int n, const int *done, int t) {
    int idx = -1, bestB = INT_MAX, bestA = INT_MAX, bestPid = INT_MAX;
    for (int i = 0; i < n; ++i) if (!done[i] && p[i].arrival <= t) {
        if (p[i].burst < bestB
            || (p[i].burst == bestB && (p[i].arrival < bestA
            || (p[i].arrival == bestA && p[i].pid < bestPid)))) {
            bestB = p[i].burst; bestA = p[i].arrival; bestPid = p[i].pid; idx = i;
        }
    }
    return idx;
}

/* among available (arrival <= t) with remaining>0, pick min remaining; tie by arrival then pid */
static int pick_srtf(const Proc *p, int n, const int *rem, const int *done, int t) {
    int idx = -1, bestR = INT_MAX, bestA = INT_MAX, bestPid = INT_MAX;
    for (int i = 0; i < n; ++i) if (!done[i] && p[i].arrival <= t && rem[i] > 0) {
        if (rem[i] < bestR
            || (rem[i] == bestR && (p[i].arrival < bestA
            || (p[i].arrival == bestA && p[i].pid < bestPid)))) {
            bestR = rem[i]; bestA = p[i].arrival; bestPid = p[i].pid; idx = i;
        }
    }
    return idx;
}

/* jump current time to next arrival (smallest arrival > t) among !done */
static int next_arrival_time(const Proc *p, int n, const int *done, int t) {
    int nextT = INT_MAX;
    for (int i = 0; i < n; ++i) if (!done[i] && p[i].arrival > t) {
        if (p[i].arrival < nextT) nextT = p[i].arrival;
    }
    return nextT;
}

/* print averages for preemptive algos using start/end and burst */
static void print_avgs_preemptive(const Proc *pr, int n, const int *start, const int *end) {
    double sumResp = 0.0, sumTAT = 0.0, sumWait = 0.0;
    for (int i = 0; i < n; ++i) {
        int resp = start[i] - pr[i].arrival;
        int tat  = end[i]   - pr[i].arrival;
        int wait = tat - pr[i].burst;
        sumResp += resp;
        sumTAT  += tat;
        sumWait += wait;
    }
    printf("Average Response Time: %.2f\n", sumResp / n);
    printf("Average Waiting Time : %.2f\n", sumWait / n);
    printf("Average Turnaround   : %.2f\n\n", sumTAT / n);
}

/* print averages for non-preemptive algos using the execution index order */
static void print_avgs_nonpreemptive(const Proc *pr, int n, const int *orderIdx /*len n*/) {
    int t = 0;
    double sumResp = 0.0, sumTAT = 0.0, sumWait = 0.0;
    for (int k = 0; k < n; ++k) {
        int i = orderIdx[k];
        if (t < pr[i].arrival) t = pr[i].arrival;   /* idle gap */
        int start = t;
        int resp  = start - pr[i].arrival;          /* = waiting for non-preemptive */
        int end   = start + pr[i].burst;
        int tat   = end - pr[i].arrival;
        int wait  = resp;
        sumResp += resp;
        sumTAT  += tat;
        sumWait += wait;
        t = end;
    }
    printf("Average Response Time: %.2f\n", sumResp / n);
    printf("Average Waiting Time : %.2f\n", sumWait / n);
    printf("Average Turnaround   : %.2f\n\n", sumTAT / n);
}

/* ---------- Algorithms ---------- */

static void FIFO(int n, const Proc *pr) {
    printf("\nFCFS (FIFO) Scheduling =>\n");

    int *done = calloc(n, sizeof(int));
    int *order = malloc(n * sizeof(int));
    if (!done || !order) { fprintf(stderr, "OOM\n"); exit(1); }

    for (int k = 0; k < n; ++k) {
        int i = pick_min_arrival(pr, n, done);
        order[k] = i;
        done[i] = 1;
    }

    printf("Execution order (by arrival): ");
    for (int k = 0; k < n; ++k) printf("%d ", pr[order[k]].pid);
    printf("\n");

    print_avgs_nonpreemptive(pr, n, order);

    free(done);
    free(order);
}

static void SJF(int n, const Proc *pr) {
    printf("SJF (Non-preemptive) Scheduling =>\n");

    int *done = calloc(n, sizeof(int));
    int *order = malloc(n * sizeof(int));
    if (!done || !order) { fprintf(stderr, "OOM\n"); exit(1); }

    int t = 0;
    for (int picked = 0; picked < n; ++picked) {
        int i = pick_sjf(pr, n, done, t);
        if (i == -1) {
            /* CPU idle: jump to next arrival then pick again */
            int nt = next_arrival_time(pr, n, done, t);
            if (nt == INT_MAX) { fprintf(stderr, "Internal error (SJF).\n"); exit(1); }
            t = nt;
            i = pick_sjf(pr, n, done, t);
        }
        order[picked] = i;
        done[i] = 1;
        if (t < pr[i].arrival) t = pr[i].arrival;
        t += pr[i].burst;
    }

    printf("Execution order (shortest among arrived): ");
    for (int k = 0; k < n; ++k) printf("%d ", pr[order[k]].pid);
    printf("\n");

    print_avgs_nonpreemptive(pr, n, order);

    free(done);
    free(order);
}

static void SRTF(int n, const Proc *pr) {
    printf("SRTF (Preemptive SJF) Scheduling =>\n");

    int *rem   = malloc(n * sizeof(int));
    int *done  = calloc(n, sizeof(int));
    int *start = malloc(n * sizeof(int));
    int *end   = malloc(n * sizeof(int));
    if (!rem || !done || !start || !end) { fprintf(stderr, "OOM\n"); exit(1); }

    for (int i = 0; i < n; ++i) {
        rem[i] = pr[i].burst;
        start[i] = -1;
        end[i] = -1;
    }

    int completed = 0;
    int t = 0;
    /* If no process at t=0, jump to earliest arrival */
    int first = pick_min_arrival(pr, n, done);
    if (first != -1 && pr[first].arrival > 0) t = pr[first].arrival;

    printf("Context switches: ");
    int last_idx = -1;

    while (completed < n) {
        int i = pick_srtf(pr, n, rem, done, t);
        if (i == -1) {
            /* idle -> jump to next arrival */
            int nt = next_arrival_time(pr, n, done, t);
            if (nt == INT_MAX) break; /* should not happen */
            t = nt;
            continue;
        }
        if (last_idx != i) {
            printf("%d ", pr[i].pid);
            last_idx = i;
        }
        if (start[i] == -1) start[i] = t;

        /* run for 1 unit to allow preemption when new arrivals appear */
        rem[i]--;
        t++;

        if (rem[i] == 0) {
            done[i] = 1;
            end[i] = t;
            completed++;
        }
    }
    printf("\n");

    print_avgs_preemptive(pr, n, start, end);

    free(rem); free(done); free(start); free(end);
}

typedef struct {
    int *q;
    int cap;
    int front, rear, size;
} Queue;

static Queue q_make(int cap) {
    Queue q; q.cap = cap; q.q = malloc(cap * sizeof(int)); q.front = q.rear = q.size = 0;
    if (!q.q) { fprintf(stderr, "OOM\n"); exit(1); }
    return q;
}
static int q_empty(Queue *q){ return q->size==0; }
static void q_push(Queue *q, int v){
    if (q->size == q->cap) { fprintf(stderr, "Queue overflow\n"); exit(1); }
    q->q[q->rear] = v; q->rear = (q->rear + 1) % q->cap; q->size++;
}
static int q_pop(Queue *q){
    if (q_empty(q)) { fprintf(stderr, "Queue underflow\n"); exit(1); }
    int v = q->q[q->front]; q->front = (q->front + 1) % q->cap; q->size--;
    return v;
}
static void q_free(Queue *q){ free(q->q); q->q = NULL; }

/* Fair RR: arrivals during a slice are enqueued before re-enqueuing the running process */
static void RoundRobin(int n, const Proc *pr, int quantum) {
    printf("Round Robin (q=%d) Scheduling =>\n", quantum);

    int *rem   = malloc(n * sizeof(int));
    int *done  = calloc(n, sizeof(int));
    int *inq   = calloc(n, sizeof(int)); /* whether currently queued */
    int *start = malloc(n * sizeof(int));
    int *end   = malloc(n * sizeof(int));
    if (!rem || !done || !inq || !start || !end) { fprintf(stderr, "OOM\n"); exit(1); }

    for (int i = 0; i < n; ++i) {
        rem[i] = pr[i].burst; start[i] = -1; end[i] = -1;
    }

    Queue q = q_make(n); /* at most one entry per unfinished process */
    int completed = 0;
    int t = 0;

    /* start at earliest arrival */
    int first = pick_min_arrival(pr, n, done);
    if (first != -1 && pr[first].arrival > 0) t = pr[first].arrival;

    /* enqueue all that have arrived by t */
    for (int i = 0; i < n; ++i) if (!done[i] && pr[i].arrival <= t && !inq[i]) {
        q_push(&q, i); inq[i] = 1;
    }

    printf("Context switches: ");
    int last_idx = -1;

    while (completed < n) {
        if (q_empty(&q)) {
            int nt = next_arrival_time(pr, n, done, t);
            if (nt == INT_MAX) break;
            t = nt;
            for (int i = 0; i < n; ++i)
                if (!done[i] && pr[i].arrival <= t && !inq[i]) { q_push(&q, i); inq[i] = 1; }
            continue;
        }

        int i = q_pop(&q); inq[i] = 0;
        if (last_idx != i) { printf("%d ", pr[i].pid); last_idx = i; }
        if (start[i] == -1) start[i] = t;

        int slice = rem[i] < quantum ? rem[i] : quantum;
        int old_t = t;
        t += slice;
        rem[i] -= slice;

        /* enqueue all new arrivals during (old_t, t] */
        for (int j = 0; j < n; ++j) if (!done[j] && !inq[j] && pr[j].arrival > old_t && pr[j].arrival <= t) {
            q_push(&q, j); inq[j] = 1;
        }

        if (rem[i] == 0) {
            done[i] = 1; end[i] = t; completed++;
        } else {
            /* not finished -> requeue current */
            if (!inq[i]) { q_push(&q, i); inq[i] = 1; }
        }
    }
    printf("\n");

    print_avgs_preemptive(pr, n, start, end);

    q_free(&q);
    free(rem); free(done); free(inq); free(start); free(end);
}

/* ---------- Main ---------- */

int main(void) {
    int n;
    printf("Number of Processes: ");
    n = read_int("number of processes");
    if (n <= 0) {
        fprintf(stderr, "ERROR: Number of processes must be positive.\n");
        return 1;
    }

    Proc *pr = malloc(n * sizeof(Proc));
    if (!pr) { fprintf(stderr, "OOM\n"); return 1; }

    printf("Enter details for each process on its own line: PID Arrival Burst\n");
    for (int i = 0; i < n; ++i) {
        int pid = read_int("PID");
        int arr = read_int("Arrival");
        int bur = read_int("Burst");
        if (arr < 0 || bur <= 0) {
            fprintf(stderr, "ERROR: Arrival must be >= 0 and Burst must be > 0.\n");
            free(pr);
            return 1;
        }
        pr[i].pid = pid;
        pr[i].arrival = arr;
        pr[i].burst = bur;
    }

    int q;
    printf("Enter Time Quantum: ");
    q = read_int("Quantum");
    if (q <= 0) {
        fprintf(stderr, "ERROR: Quantum must be > 0.\n");
        free(pr);
        return 1;
    }

    FIFO(n, pr);
    SJF(n, pr);
    SRTF(n, pr);
    RoundRobin(n, pr, q);

    free(pr);
    return 0;
}

