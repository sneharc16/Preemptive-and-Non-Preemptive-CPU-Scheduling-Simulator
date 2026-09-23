Number of Processes: Enter details for each process on its own line: PID Arrival Burst

FCFS (FIFO) Scheduling =>
Gantt — FCFS:
[0  ,5  ) P1   | [5  ,8  ) P2   | [8  ,16 ) P3   | [16 ,22 ) P4   | [22 ,26 ) P5   

Per-tick timeline — FCFS:
t=0: P1
t=1: P1
t=2: P1
t=3: P1
t=4: P1
t=5: P2
t=6: P2
t=7: P2
t=8: P3
t=9: P3
t=10: P3
t=11: P3
t=12: P3
t=13: P3
t=14: P3
t=15: P3
t=16: P4
t=17: P4
t=18: P4
t=19: P4
t=20: P4
t=21: P4
t=22: P5
t=23: P5
t=24: P5
t=25: P5

FCFS Averages:
  Response:  8.20
  Waiting :  8.20
  Turnaround:13.40

SJF (Non-preemptive) Scheduling =>
Gantt — SJF:
[0  ,5  ) P1   | [5  ,8  ) P2   | [8  ,12 ) P5   | [12 ,18 ) P4   | [18 ,26 ) P3   

Per-tick timeline — SJF:
t=0: P1
t=1: P1
t=2: P1
t=3: P1
t=4: P1
t=5: P2
t=6: P2
t=7: P2
t=8: P5
t=9: P5
t=10: P5
t=11: P5
t=12: P4
t=13: P4
t=14: P4
t=15: P4
t=16: P4
t=17: P4
t=18: P3
t=19: P3
t=20: P3
t=21: P3
t=22: P3
t=23: P3
t=24: P3
t=25: P3

SJF Averages:
  Response:  6.60
  Waiting :  6.60
  Turnaround:11.80

SRTF (Preemptive SJF) Scheduling =>
Gantt — SRTF:
[0  ,1  ) P1   | [1  ,4  ) P2   | [4  ,8  ) P1   | [8  ,12 ) P5   | [12 ,18 ) P4   | [18 ,26 ) P3   

Per-tick timeline — SRTF:
t=0: P1
t=1: P2
t=2: P2
t=3: P2
t=4: P1
t=5: P1
t=6: P1
t=7: P1
t=8: P5
t=9: P5
t=10: P5
t=11: P5
t=12: P4
t=13: P4
t=14: P4
t=15: P4
t=16: P4
t=17: P4
t=18: P3
t=19: P3
t=20: P3
t=21: P3
t=22: P3
t=23: P3
t=24: P3
t=25: P3

SRTF Averages:
  Response:  5.80
  Waiting :  6.40
  Turnaround:11.60

Round Robin Scheduling (q=2) =>
Gantt — RoundRobin(q=2):
[0  ,2  ) P1   | [2  ,4  ) P2   | [4  ,6  ) P3   | [6  ,8  ) P1   | [8  ,10 ) P4   | [10 ,12 ) P5   | [12 ,13 ) P2   | [13 ,15 ) P3   | [15 ,16 ) P1   | [16 ,18 ) P4   | [18 ,20 ) P5   | [20 ,22 ) P3   | [22 ,24 ) P4   | [24 ,26 ) P3   

Per-tick timeline — RoundRobin(q=2):
t=0: P1
t=1: P1
t=2: P2
t=3: P2
t=4: P3
t=5: P3
t=6: P1
t=7: P1
t=8: P4
t=9: P4
t=10: P5
t=11: P5
t=12: P2
t=13: P3
t=14: P3
t=15: P1
t=16: P4
t=17: P4
t=18: P5
t=19: P5
t=20: P3
t=21: P3
t=22: P4
t=23: P4
t=24: P3
t=25: P3

RoundRobin(q=2) Averages:
  Response:  2.80
  Waiting :  12.60
  Turnaround:17.80

CSV written: out.csv
