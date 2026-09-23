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

Round Robin Scheduling (q=5) =>
Gantt — RoundRobin(q=5):
[0  ,4  ) P1   | [4  ,6  ) P2   | [6  ,11 ) P3   | [11 ,14 ) P4   | [14 ,15 ) P5   | [15 ,16 ) P3   

RoundRobin(q=5) Averages:
  Response:  7.00
  Waiting :  7.80
  Turnaround:11.00

CSV written: out.csv
