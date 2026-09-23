Number of Processes: Enter details for each process on its own line: PID Arrival Burst

FCFS (FIFO) Scheduling =>
Gantt — FCFS:
[0  ,4  ) P1   | [4  ,6  ) P2   | [6  ,12 ) P3   | [12 ,15 ) P4   | [15 ,16 ) P5   

FCFS Averages:
  Response:  7.40
  Waiting :  7.40
  Turnaround:10.60

SJF (Non-preemptive) Scheduling =>
Gantt — SJF:
[0  ,1  ) P5   | [1  ,3  ) P2   | [3  ,6  ) P4   | [6  ,10 ) P1   | [10 ,16 ) P3   

SJF Averages:
  Response:  4.00
  Waiting :  4.00
  Turnaround:7.20

SRTF (Preemptive SJF) Scheduling =>
Gantt — SRTF:
[0  ,1  ) P5   | [1  ,3  ) P2   | [3  ,6  ) P4   | [6  ,10 ) P1   | [10 ,16 ) P3   

SRTF Averages:
  Response:  4.00
  Waiting :  4.00
  Turnaround:7.20

Round Robin Scheduling (q=3) =>
Gantt — RoundRobin(q=3):
[0  ,3  ) P1   | [3  ,5  ) P2   | [5  ,8  ) P3   | [8  ,11 ) P4   | [11 ,12 ) P5   | [12 ,13 ) P1   | [13 ,16 ) P3   

RoundRobin(q=3) Averages:
  Response:  5.40
  Waiting :  8.20
  Turnaround:11.40

CSV written: out.csv
