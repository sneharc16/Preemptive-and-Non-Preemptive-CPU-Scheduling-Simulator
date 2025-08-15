// sched_opt.c — Event-driven, flag-driven CPU schedulers: FCFS, SJF, SRTF, RR
// Build: gcc -O2 -std=c11 -Wall -Wextra sched_opt.c -o sched

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>

/* ===================== Data types ===================== */

typedef struct { int pid, arrival, burst; } Proc;

typedef struct { int pid; int start; int end; } Seg; /* pid = -1 => IDLE */
typedef struct {
    Seg *a; int len, cap;
} SegVec;

static void seg_reserve(SegVec *v, int need){
    if (need <= v->cap) return;
    int ncap = v->cap ? v->cap : 8;
    while (ncap < need) ncap <<= 1;
    Seg *na = (Seg*)realloc(v->a, ncap * sizeof(Seg));
    if (!na) { fprintf(stderr,"OOM\n"); exit(1); }
    v->a = na; v->cap = ncap;
}
static void seg_push(SegVec *v, Seg s){ seg_reserve(v, v->len+1); v->a[v->len++] = s; }
static void seg_coalesce(SegVec *v){
    if (v->len <= 1) return;
    int w = 0;
    for (int i=0;i<v->len;i++){
        if (w==0){ v->a[w++] = v->a[i]; continue; }
        if (v->a[i].pid == v->a[w-1].pid && v->a[i].start == v->a[w-1].end) {
            v->a[w-1].end = v->a[i].end;
        } else v->a[w++] = v->a[i];
    }
    v->len = w;
}
static void seg_free(SegVec *v){ free(v->a); v->a=NULL; v->len=v->cap=0; }

/* ===================== CLI config ===================== */

typedef struct {
    bool run_fcfs, run_sjf, run_srtf, run_rr;
    int quantum;
    bool print_gantt;
    bool print_pertick;
    bool write_csv;
    char csv_path[256];
} Config;

static void config_default(Config *c){
    c->run_fcfs = c->run_sjf = c->run_srtf = c->run_rr = true;
    c->quantum = 2;
    c->print_gantt = true;
    c->print_pertick = false;
    c->write_csv = true;
    strcpy(c->csv_path, "schedule_metrics.csv");
}
static void print_help(const char *prog){
    printf("Usage: %s [--algo=all|fcfs,sjf,srtf,rr] [--quantum=Q] [--csv=FILE|--no-csv] [--no-gantt] [--per-tick]\n", prog);
}

static void parse_algos(Config *c, const char *val){
    c->run_fcfs = c->run_sjf = c->run_srtf = c->run_rr = false;
    if (strcmp(val,"all")==0){ c->run_fcfs=c->run_sjf=c->run_srtf=c->run_rr=true; return; }
    char buf[128]; strncpy(buf,val,sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    char *tok = strtok(buf,",");
    while (tok){
        if      (!strcmp(tok,"fcfs")) c->run_fcfs=true;
        else if (!strcmp(tok,"sjf"))  c->run_sjf =true;
        else if (!strcmp(tok,"srtf")) c->run_srtf=true;
        else if (!strcmp(tok,"rr"))   c->run_rr  =true;
        else { fprintf(stderr,"Unknown algo: %s\n", tok); exit(1); }
        tok = strtok(NULL,",");
    }
}

static void parse_args(Config *c, int argc, char **argv){
    for (int i=1;i<argc;i++){
        if (!strncmp(argv[i],"--algo=",8)) parse_algos(c, argv[i]+8);
        else if (!strncmp(argv[i],"--quantum=",10)) c->quantum = atoi(argv[i]+10);
        else if (!strcmp(argv[i],"--no-gantt")) c->print_gantt = false;
        else if (!strcmp(argv[i],"--per-tick")) c->print_pertick = true;
        else if (!strcmp(argv[i],"--no-csv")) c->write_csv = false;
        else if (!strncmp(argv[i],"--csv=",6)) { c->write_csv = true; strncpy(c->csv_path, argv[i]+6, sizeof(c->csv_path)-1); c->csv_path[sizeof(c->csv_path)-1]='\0'; }
        else if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) { print_help(argv[0]); exit(0); }
        else { fprintf(stderr,"Unknown option: %s\n", argv[i]); print_help(argv[0]); exit(1); }
    }
    if (c->quantum <= 0){ fprintf(stderr,"Quantum must be > 0\n"); exit(1); }
}

/* ===================== IO helpers ===================== */

static int read_int(const char *label){
    int x; if (scanf("%d",&x)!=1){ fprintf(stderr,"ERROR: Failed to read %s\n", label); exit(1); }
    return x;
}

/* ===================== Metrics & CSV ===================== */

typedef struct {
    FILE *f;
    bool open;
} Csv;

static void csv_open(Csv *c, const Config *cfg){
    if (!cfg->write_csv){ c->open=false; c->f=NULL; return; }
    c->f = fopen(cfg->csv_path, "w");
    if (!c->f){ fprintf(stderr,"ERROR: cannot open %s for writing\n", cfg->csv_path); exit(1); }
    fprintf(c->f, "Algorithm,PID,Arrival,Burst,Start,Completion,Response,Waiting,Turnaround\n");
    c->open = true;
}
static void csv_close(Csv *c){ if (c->open){ fclose(c->f); c->open=false; } }

static void csv_dump_algo(Csv *csv, const char *alg, const Proc *pr, int n, const int *start, const int *end){
    if (!csv->open) return;
    for (int i=0;i<n;i++){
        int resp = start[i] - pr[i].arrival;
        int tat  = end[i]   - pr[i].arrival;
        int wait = tat - pr[i].burst;
        fprintf(csv->f, "%s,%d,%d,%d,%d,%d,%d,%d,%d\n",
                alg, pr[i].pid, pr[i].arrival, pr[i].burst, start[i], end[i], resp, wait, tat);
    }
}

static void print_avgs(const char *alg, const Proc *pr, int n, const int *start, const int *end){
    double sr=0, sw=0, st=0;
    for (int i=0;i<n;i++){
        int resp = start[i] - pr[i].arrival;
        int tat  = end[i]   - pr[i].arrival;
        int wait = tat - pr[i].burst;
        sr += resp; sw += wait; st += tat;
    }
    printf("%s Averages:\n  Response:  %.2f\n  Waiting :  %.2f\n  Turnaround:%.2f\n\n",
           alg, sr/n, sw/n, st/n);
}

/* visuals */
static void print_gantt(const char *alg, const SegVec *sv, const Config *cfg){
    if (!cfg->print_gantt) return;
    printf("Gantt — %s:\n", alg);
    if (sv->len==0){ printf("(empty)\n\n"); return; }
    for (int i=0;i<sv->len;i++){
        if (sv->a[i].pid==-1) printf("[%-3d,%-3d) IDLE  ", sv->a[i].start, sv->a[i].end);
        else                  printf("[%-3d,%-3d) P%-4d", sv->a[i].start, sv->a[i].end, sv->a[i].pid);
        if (i+1<sv->len) printf("| ");
    }
    printf("\n\n");
}
static void print_pertick(const char *alg, const SegVec *sv, const Config *cfg){
    if (!cfg->print_pertick) return;
    printf("Per-tick timeline — %s:\n", alg);
    for (int i=0;i<sv->len;i++){
        for (int t=sv->a[i].start; t<sv->a[i].end; ++t){
            if (sv->a[i].pid==-1) printf("t=%d: IDLE\n", t);
            else                  printf("t=%d: P%d\n", t, sv->a[i].pid);
        }
    }
    printf("\n");
}

/* ===================== Sorting helpers ===================== */
/* Portable qsort comparator using a global context (safe in this single-threaded tool). */
static const Proc *g_pr_sort = NULL;
static int cmp_arrival_pid_index(const void *pa, const void *pb){
    int ia = *(const int*)pa, ib = *(const int*)pb;
    const Proc *pr = g_pr_sort;
    if (pr[ia].arrival != pr[ib].arrival) return (pr[ia].arrival < pr[ib].arrival) ? -1 : 1;
    if (pr[ia].pid     != pr[ib].pid)     return (pr[ia].pid     < pr[ib].pid)     ? -1 : 1;
    return 0;
}

/* ===================== Min-heaps ===================== */

typedef struct {
    int *h; int sz, cap;
} Heap;

static void heap_init(Heap *hp, int cap){ hp->h=(int*)malloc(sizeof(int)*cap); hp->sz=0; hp->cap=cap; if(!hp->h){fprintf(stderr,"OOM\n");exit(1);} }
static void heap_free(Heap *hp){ free(hp->h); hp->h=NULL; hp->sz=hp->cap=0; }
static void heap_ensure(Heap *hp){ if (hp->sz < hp->cap) return; hp->cap = hp->cap ? hp->cap*2 : 16; hp->h=(int*)realloc(hp->h,sizeof(int)*hp->cap); if(!hp->h){fprintf(stderr,"OOM\n");exit(1);} }

/* SJF comparator: burst, arrival, pid */
static bool less_sjf(const Proc *pr, int i, int j){
    if (pr[i].burst != pr[j].burst) return pr[i].burst < pr[j].burst;
    if (pr[i].arrival != pr[j].arrival) return pr[i].arrival < pr[j].arrival;
    return pr[i].pid < pr[j].pid;
}
static void heap_push_sjf(Heap *hp, const Proc *pr, int idx){
    heap_ensure(hp); int i = hp->sz++; hp->h[i]=idx;
    while (i>0){ int p=(i-1)/2; if (less_sjf(pr,hp->h[i],hp->h[p])){ int t=hp->h[i]; hp->h[i]=hp->h[p]; hp->h[p]=t; i=p; } else break; }
}
static int heap_pop_sjf(Heap *hp, const Proc *pr){
    int ret = hp->h[0]; hp->h[0]=hp->h[--hp->sz];
    int i=0;
    for(;;){
        int l=2*i+1, r=2*i+2, m=i;
        if (l<hp->sz && less_sjf(pr,hp->h[l],hp->h[m])) m=l;
        if (r<hp->sz && less_sjf(pr,hp->h[r],hp->h[m])) m=r;
        if (m==i) break;
        int t=hp->h[i]; hp->h[i]=hp->h[m]; hp->h[m]=t; i=m;
    }
    return ret;
}

/* SRTF comparator: remaining, arrival, pid */
static bool less_srtf(const Proc *pr, const int *rem, int i, int j){
    if (rem[i] != rem[j]) return rem[i] < rem[j];
    if (pr[i].arrival != pr[j].arrival) return pr[i].arrival < pr[j].arrival;
    return pr[i].pid < pr[j].pid;
}
static void heap_push_srtf(Heap *hp, const Proc *pr, const int *rem, int idx){
    heap_ensure(hp); int i = hp->sz++; hp->h[i]=idx;
    while (i>0){ int p=(i-1)/2; if (less_srtf(pr,rem,hp->h[i],hp->h[p])){ int t=hp->h[i]; hp->h[i]=hp->h[p]; hp->h[p]=t; i=p; } else break; }
}
static int heap_pop_srtf(Heap *hp, const Proc *pr, const int *rem){
    int ret = hp->h[0]; hp->h[0]=hp->h[--hp->sz];
    int i=0;
    for(;;){
        int l=2*i+1, r=2*i+2, m=i;
        if (l<hp->sz && less_srtf(pr,rem,hp->h[l],hp->h[m])) m=l;
        if (r<hp->sz && less_srtf(pr,rem,hp->h[r],hp->h[m])) m=r;
        if (m==i) break;
        int t=hp->h[i]; hp->h[i]=hp->h[m]; hp->h[m]=t; i=m;
    }
    return ret;
}

/* ===================== Algorithms ===================== */

static void run_fcfs(const Proc *pr, int n, Csv *csv, const Config *cfg){
    const char *ALG = "FCFS";
    int *idx = (int*)malloc(n*sizeof(int));
    int *start=(int*)malloc(n*sizeof(int)), *end=(int*)malloc(n*sizeof(int));
    if(!idx||!start||!end){fprintf(stderr,"OOM\n");exit(1);}
    for (int i=0;i<n;i++) idx[i]=i;

    /* sort by (arrival, pid) */
    g_pr_sort = pr;
    qsort(idx, n, sizeof(int), cmp_arrival_pid_index);

    SegVec sv={0};
    int t=0;
    for (int k=0;k<n;k++){
        int i = idx[k];
        if (t < pr[i].arrival){ seg_push(&sv, (Seg){.pid=-1,.start=t,.end=pr[i].arrival}); t = pr[i].arrival; }
        start[i]=t;
        seg_push(&sv, (Seg){.pid=pr[i].pid,.start=t,.end=t+pr[i].burst});
        t += pr[i].burst;
        end[i]=t;
    }
    seg_coalesce(&sv);

    printf("\nFCFS (FIFO) Scheduling =>\n");
    print_gantt(ALG, &sv, cfg);
    print_pertick(ALG, &sv, cfg);
    print_avgs(ALG, pr, n, start, end);
    csv_dump_algo(csv, ALG, pr, n, start, end);

    seg_free(&sv); free(idx); free(start); free(end);
}

static void run_sjf(const Proc *pr, int n, Csv *csv, const Config *cfg){
    const char *ALG = "SJF";
    int *start=(int*)malloc(n*sizeof(int)), *end=(int*)malloc(n*sizeof(int));
    if(!start||!end){fprintf(stderr,"OOM\n");exit(1);}

    int *ord = (int*)malloc(n*sizeof(int));
    for (int i=0;i<n;i++) ord[i]=i;
    g_pr_sort = pr;
    qsort(ord, n, sizeof(int), cmp_arrival_pid_index);

    Heap hp; heap_init(&hp, n);
    SegVec sv={0};
    int t = 0, k = 0, doneCnt=0;

    if (n>0 && pr[ord[0]].arrival>t) t = pr[ord[0]].arrival;

    while (doneCnt < n){
        while (k<n && pr[ord[k]].arrival <= t){ heap_push_sjf(&hp, pr, ord[k]); k++; }
        if (hp.sz==0){
            if (k<n){
                if (t < pr[ord[k]].arrival) seg_push(&sv,(Seg){.pid=-1,.start=t,.end=pr[ord[k]].arrival});
                t = pr[ord[k]].arrival;
                continue;
            } else break;
        }
        int i = heap_pop_sjf(&hp, pr);
        start[i] = t;
        seg_push(&sv, (Seg){.pid=pr[i].pid,.start=t,.end=t+pr[i].burst});
        t += pr[i].burst;
        end[i] = t; doneCnt++;
    }
    seg_coalesce(&sv);

    printf("SJF (Non-preemptive) Scheduling =>\n");
    print_gantt(ALG, &sv, cfg);
    print_pertick(ALG, &sv, cfg);
    print_avgs(ALG, pr, n, start, end);
    csv_dump_algo(csv, ALG, pr, n, start, end);

    seg_free(&sv); heap_free(&hp); free(ord); free(start); free(end);
}

static void run_srtf(const Proc *pr, int n, Csv *csv, const Config *cfg){
    const char *ALG = "SRTF";
    int *start=(int*)malloc(n*sizeof(int)), *end=(int*)malloc(n*sizeof(int)), *rem=(int*)malloc(n*sizeof(int));
    if(!start||!end||!rem){fprintf(stderr,"OOM\n");exit(1);}
    for (int i=0;i<n;i++){ start[i]=-1; end[i]=-1; rem[i]=pr[i].burst; }

    int *ord = (int*)malloc(n*sizeof(int));
    for (int i=0;i<n;i++) ord[i]=i;
    g_pr_sort = pr;
    qsort(ord, n, sizeof(int), cmp_arrival_pid_index);

    Heap hp; heap_init(&hp, n);
    SegVec sv={0};

    int t=0, k=0, completed=0;
    if (n>0 && pr[ord[0]].arrival>t) t = pr[ord[0]].arrival;

    int cur = -2; int seg_start = t;

    while (completed < n){
        while (k<n && pr[ord[k]].arrival <= t){ heap_push_srtf(&hp, pr, rem, ord[k]); k++; }
        if (hp.sz==0){
            if (k<n){
                if (cur!=-1){ if (cur!=-2) seg_push(&sv,(Seg){.pid=cur,.start=seg_start,.end=t}); cur=-1; seg_start=t; }
                seg_push(&sv,(Seg){.pid=-1,.start=t,.end=pr[ord[k]].arrival});
                t = pr[ord[k]].arrival;
                cur = -2; seg_start = t;
                continue;
            } else break;
        }
        int i = hp.h[0]; /* peek current shortest remaining */
        if (start[i]==-1) start[i]=t;

        int next_arrival = (k<n) ? pr[ord[k]].arrival : INT_MAX;
        int finish_time  = t + rem[i];

        if (finish_time <= next_arrival){
            if (cur != pr[i].pid){ if (cur!=-2) seg_push(&sv,(Seg){.pid=cur,.start=seg_start,.end=t}); cur = pr[i].pid; seg_start=t; }
            t = finish_time; rem[i]=0;
            (void)heap_pop_srtf(&hp, pr, rem);
            end[i]=t; completed++;
        } else {
            int run_len = next_arrival - t;
            if (cur != pr[i].pid){ if (cur!=-2) seg_push(&sv,(Seg){.pid=cur,.start=seg_start,.end=t}); cur = pr[i].pid; seg_start=t; }
            t += run_len; rem[i] -= run_len;
            (void)heap_pop_srtf(&hp, pr, rem);
            heap_push_srtf(&hp, pr, rem, i);
        }
    }
    if (cur!=-2) seg_push(&sv,(Seg){.pid=cur,.start=seg_start,.end=t});
    seg_coalesce(&sv);

    printf("SRTF (Preemptive SJF) Scheduling =>\n");
    print_gantt(ALG, &sv, cfg);
    print_pertick(ALG, &sv, cfg);
    print_avgs(ALG, pr, n, start, end);
    csv_dump_algo(csv, ALG, pr, n, start, end);

    seg_free(&sv); heap_free(&hp); free(ord); free(start); free(end); free(rem);
}

/* Simple circular queue for RR */
typedef struct { int *q; int cap, front, rear, size; } Queue;
static void q_init(Queue *q,int cap){ q->q=(int*)malloc(cap*sizeof(int)); q->cap=cap; q->front=q->rear=q->size=0; if(!q->q){fprintf(stderr,"OOM\n");exit(1);} }
static bool q_empty(Queue *q){ return q->size==0; }
static void q_push(Queue *q,int v){ if(q->size==q->cap){fprintf(stderr,"Queue overflow\n");exit(1);} q->q[q->rear]=v; q->rear=(q->rear+1)%q->cap; q->size++; }
static int q_pop(Queue *q){ if(q_empty(q)){fprintf(stderr,"Queue underflow\n");exit(1);} int v=q->q[q->front]; q->front=(q->front+1)%q->cap; q->size--; return v; }
static void q_free(Queue *q){ free(q->q); }

static void run_rr(const Proc *pr, int n, int quantum, Csv *csv, const Config *cfg){
    char ALG[64]; snprintf(ALG,sizeof(ALG),"RoundRobin(q=%d)",quantum);
    int *start=(int*)malloc(n*sizeof(int)), *end=(int*)malloc(n*sizeof(int)), *rem=(int*)malloc(n*sizeof(int)), *inq=(int*)calloc(n,sizeof(int));
    if(!start||!end||!rem||!inq){fprintf(stderr,"OOM\n");exit(1);}
    for (int i=0;i<n;i++){ start[i]=-1; end[i]=-1; rem[i]=pr[i].burst; }

    int *ord = (int*)malloc(n*sizeof(int)); for(int i=0;i<n;i++) ord[i]=i;
    g_pr_sort = pr;
    qsort(ord, n, sizeof(int), cmp_arrival_pid_index);

    Queue q; q_init(&q, n);
    SegVec sv={0};

    int t=0, k=0, completed=0;
    if (n>0 && pr[ord[0]].arrival>t) t = pr[ord[0]].arrival;

    while (k<n && pr[ord[k]].arrival <= t){ q_push(&q, ord[k]); inq[ord[k]]=1; k++; }

    int cur_pid=-2; int seg_start=t;

    while (completed < n){
        if (q_empty(&q)){
            if (k<n){
                if (cur_pid!=-1){ if (cur_pid!=-2) seg_push(&sv,(Seg){.pid=cur_pid,.start=seg_start,.end=t}); cur_pid=-1; seg_start=t; }
                seg_push(&sv,(Seg){.pid=-1,.start=t,.end=pr[ord[k]].arrival});
                t = pr[ord[k]].arrival;
                cur_pid=-2; seg_start=t;
                while (k<n && pr[ord[k]].arrival <= t){ q_push(&q, ord[k]); inq[ord[k]]=1; k++; }
                continue;
            } else break;
        }

        int i = q_pop(&q); inq[i]=0;
        if (start[i]==-1) start[i]=t;

        if (cur_pid != pr[i].pid){
            if (cur_pid!=-2) seg_push(&sv,(Seg){.pid=cur_pid,.start=seg_start,.end=t});
            cur_pid = pr[i].pid; seg_start=t;
        }

        int slice = rem[i] < quantum ? rem[i] : quantum;
        int old_t = t; t += slice; rem[i] -= slice;

        while (k<n && pr[ord[k]].arrival <= t){ if(!inq[ord[k]]){ q_push(&q, ord[k]); inq[ord[k]]=1; } k++; }

        if (rem[i]==0){ end[i]=t; completed++; }
        else { q_push(&q, i); inq[i]=1; }
    }
    if (cur_pid!=-2) seg_push(&sv,(Seg){.pid=cur_pid,.start=seg_start,.end=t});
    seg_coalesce(&sv);

    printf("Round Robin Scheduling (q=%d) =>\n", quantum);
    print_gantt(ALG, &sv, cfg);
    print_pertick(ALG, &sv, cfg);
    print_avgs(ALG, pr, n, start, end);
    csv_dump_algo(csv, ALG, pr, n, start, end);

    seg_free(&sv); q_free(&q); free(ord); free(start); free(end); free(rem); free(inq);
}

/* ===================== Main ===================== */

int main(int argc, char **argv){
    Config cfg; config_default(&cfg); parse_args(&cfg, argc, argv);

    printf("Number of Processes: ");
    int n = read_int("number of processes");
    if (n <= 0){ fprintf(stderr,"ERROR: n must be positive\n"); return 1; }

    Proc *pr = (Proc*)malloc(n*sizeof(Proc));
    if (!pr){ fprintf(stderr,"OOM\n"); return 1; }

    printf("Enter details for each process on its own line: PID Arrival Burst\n");
    for (int i=0;i<n;i++){
        int pid = read_int("PID");
        int arr = read_int("Arrival");
        int bur = read_int("Burst");
        if (arr < 0 || bur <= 0){ fprintf(stderr,"ERROR: Arrival >= 0, Burst > 0\n"); free(pr); return 1; }
        pr[i].pid=pid; pr[i].arrival=arr; pr[i].burst=bur;
    }

    Csv csv; csv_open(&csv, &cfg);

    if (cfg.run_fcfs) run_fcfs(pr, n, &csv, &cfg);
    if (cfg.run_sjf)  run_sjf (pr, n, &csv, &cfg);
    if (cfg.run_srtf) run_srtf(pr, n, &csv, &cfg);
    if (cfg.run_rr)   run_rr  (pr, n, cfg.quantum, &csv, &cfg);

    if (csv.open){ csv_close(&csv); printf("CSV written: %s\n", cfg.csv_path); }
    free(pr);
    return 0;
}

