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

Round Robin Scheduling (q=1) =>
Gantt — RoundRobin(q=1):
[0  ,1  ) P1   | [1  ,2  ) P2   | [2  ,3  ) P1   | [3  ,4  ) P3   | [4  ,5  ) P2   | [5  ,6  ) P4   | [6  ,7  ) P1   | [7  ,8  ) P5   | [8  ,9  ) P3   | [9  ,10 ) P2   | [10 ,11 ) P4   | [11 ,12 ) P1   | [12 ,13 ) P5   | [13 ,14 ) P3   | [14 ,15 ) P4   | [15 ,16 ) P1   | [16 ,17 ) P5   | [17 ,18 ) P3   | [18 ,19 ) P4   | [19 ,20 ) P5   | [20 ,21 ) P3   | [21 ,22 ) P4   | [22 ,23 ) P3   | [23 ,24 ) P4   | [24 ,26 ) P3   

RoundRobin(q=1) Averages:
  Response:  1.20
  Waiting :  12.00
  Turnaround:17.20

CSV written: out.csv
