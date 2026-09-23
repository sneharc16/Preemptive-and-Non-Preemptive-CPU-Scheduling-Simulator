# macOS 'sample' profiles (top of stack, sample counts)

Workload: tools/gen.py --load 2 (overloaded), --algo=prio-p --aging=5, Apple M1.

## Before (commit 4d25a8e, linear scan), n = 40,000, 5 s sample

```
Sort by top of stack, same collapsed (when >= 5):
        prio_next_slice  (in sched)        4221
```

## After (treap), n = 1,000,000, 2 s sample

```
Sort by top of stack, same collapsed (when >= 5):
        prio_next_slice  (in sched)        560
        pull  (in sched)        536
        insert  (in sched)        271
        erase  (in sched)        149
        process_instant  (in sched)        29
        split  (in sched)        26
        append  (in sched)        23
        merge  (in sched)        20
        sim_run  (in sched)        15
        prio_requeue  (in sched)        12
        end_run  (in sched)        11
        dispatch_one  (in sched)        10
```
