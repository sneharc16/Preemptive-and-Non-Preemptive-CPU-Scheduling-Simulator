Number of Processes: Enter details for each process on its own line: PID Arrival Burst

FCFS (FIFO) Scheduling =>
Gantt — FCFS:
[0  ,5  ) P1   | [5  ,8  ) P2   | [8  ,16 ) P3   | [16 ,22 ) P4   | [22 ,26 ) P5   

FCFS Averages:
  Response:  8.20
  Waiting :  8.20
  Turnaround:13.40

SJF (Non-preemptive) Scheduling =>
Gantt — SJF:
[0  ,5  ) P1   | [5  ,8  ) P2   | [8  ,12 ) P5   | [12 ,18 ) P4   | [18 ,26 ) P3   

SJF Averages:
  Response:  6.60
  Waiting :  6.60
  Turnaround:11.80

SRTF (Preemptive SJF) Scheduling =>
Gantt — SRTF:
[0  ,1  ) P1   | [1  ,4  ) P2   | [4  ,8  ) P1   | [8  ,12 ) P5   | [12 ,18 ) P4   | [18 ,26 ) P3   

SRTF Averages:
  Response:  5.80
  Waiting :  6.40
  Turnaround:11.60

Round Robin Scheduling (q=3) =>
Gantt — RoundRobin(q=3):
[0  ,3  ) P1   | [3  ,6  ) P2   | [6  ,9  ) P3   | [9  ,12 ) P4   | [12 ,14 ) P1   | [14 ,17 ) P5   | [17 ,20 ) P3   | [20 ,23 ) P4   | [23 ,24 ) P5   | [24 ,26 ) P3   

RoundRobin(q=3) Averages:
  Response:  4.40
  Waiting :  11.40
  Turnaround:16.60

CSV written: out.csv
