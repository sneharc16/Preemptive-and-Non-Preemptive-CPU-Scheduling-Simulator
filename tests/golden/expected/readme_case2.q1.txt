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

Round Robin Scheduling (q=1) =>
Gantt — RoundRobin(q=1):
[0  ,1  ) P1   | [1  ,2  ) P2   | [2  ,3  ) P3   | [3  ,4  ) P4   | [4  ,5  ) P5   | [5  ,6  ) P1   | [6  ,7  ) P2   | [7  ,8  ) P3   | [8  ,9  ) P4   | [9  ,10 ) P1   | [10 ,11 ) P3   | [11 ,12 ) P4   | [12 ,13 ) P1   | [13 ,16 ) P3   

RoundRobin(q=1) Averages:
  Response:  2.00
  Waiting :  7.40
  Turnaround:10.60

CSV written: out.csv
