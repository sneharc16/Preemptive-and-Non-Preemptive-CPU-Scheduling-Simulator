#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

/**
 * CPU Scheduling Algorithms Implementation
 * Implements FIFO, SJF, SRTF, and Round Robin scheduling algorithms
 * Tech Stack: C
 */

/**
 * Finds the process with minimum arrival time for FIFO scheduling
 * @param arr: Array of arrival times
 * @param n: Number of processes
 * @param check: Array to track processed elements
 * @return: Index of process with minimum arrival time, -1 if none found
 */
int min_index_fifo(int arr[], int n, int check[]) {
    int mini = -1;
    int minValue = INT_MAX;

    // Find the process with earliest arrival time that hasn't been processed
    for (int i = 0; i < n; i++) {
        if (!check[i] && arr[i] < minValue) {
            minValue = arr[i];
            mini = i;
        }
    }
    return mini;
}

/**
 * Finds the process with minimum burst time for SJF scheduling
 * Only considers processes that have arrived by current_time
 * @param arr: Array of arrival times
 * @param burst: Array of burst times
 * @param n: Number of processes
 * @param check: Array to track processed elements
 * @param current_time: Current simulation time
 * @return: Index of process with minimum burst time, -1 if none found
 */
int min_index_sjf(int arr[], int burst[], int n, int check[], int current_time) {
    int mini = -1;
    int minBurst = INT_MAX;

    // Find the process with shortest burst time among arrived processes
    for (int i = 0; i < n; i++) {
        if (!check[i] && arr[i] <= current_time) {
            if (burst[i] < minBurst) {
                minBurst = burst[i];
                mini = i;
            }
        }
    }
    return mini;
}

/**
 * Calculates and displays average response time and turnaround time
 * Used for non-preemptive algorithms (FIFO, SJF)
 * @param n: Number of processes
 * @param arrival: Array of arrival times
 * @param burst: Array of burst times
 * @param execution: Array showing execution order (process IDs)
 */
void calculate_times_non_preemptive(int n, int arrival[], int burst[], int execution[]) {
    float total_turnaround = 0;
    float total_response = 0;
    int current_time = 0;

    // Calculate times for each process in execution order
    for (int i = 0; i < n; i++) {
        int process_index = execution[i] - 1; // Convert PID to array index
        
        // If current time is less than arrival time, CPU is idle
        if (current_time < arrival[process_index]) {
            current_time = arrival[process_index];
        }
        
        // Response time = start time - arrival time
        total_response += current_time - arrival[process_index];
        
        // Process executes
        current_time += burst[process_index];
        
        // Turnaround time = completion time - arrival time
        total_turnaround += current_time - arrival[process_index];
    }

    printf("Average Response Time: %.2f\n", total_response / n);
    printf("Average Turnaround Time: %.2f\n", total_turnaround / n);
    printf("\n");
}

/**
 * Calculates and displays average response time and turnaround time
 * Used for preemptive algorithms (SRTF, Round Robin)
 * @param n: Number of processes
 * @param arrival: Array of arrival times
 * @param start_time: Array of first execution times
 * @param end_time: Array of completion times
 */
void calculate_times_preemptive(int n, int arrival[], int start_time[], int end_time[]) {
    float turnaround = 0;
    float response = 0;

    // Calculate turnaround and response times for each process
    for (int i = 0; i < n; i++) {
        turnaround += end_time[i] - arrival[i];    // Completion - Arrival
        response += start_time[i] - arrival[i];    // First execution - Arrival
    }

    turnaround /= n;
    response /= n;

    printf("Average Response Time: %.2f\n", response);
    printf("Average Turnaround Time: %.2f\n", turnaround);
    printf("\n");
}

/**
 * First In First Out (FIFO) Scheduling Algorithm
 * Non-preemptive algorithm that executes processes in order of arrival
 * @param n: Number of processes
 * @param pid: Array of process IDs
 * @param arrival: Array of arrival times
 * @param burst: Array of burst times
 */
void FIFO(int n, int pid[], int arrival[], int burst[]) {
    printf("\nFIFO Scheduling Algorithm =>\n");
    int execution[n];
    int check[n];

    // Initialize check array
    for (int i = 0; i < n; i++) {
        check[i] = 0;
    }

    // Determine execution order based on arrival times
    for (int i = 0; i < n; i++) {
        int a = min_index_fifo(arrival, n, check);
        if (a != -1) {
            execution[i] = pid[a];
            check[a] = 1;
        }
    }

    // Display execution order
    printf("Execution order: ");
    for (int i = 0; i < n; i++) {
        printf("%d ", execution[i]);
    }
    printf("\n");

    calculate_times_non_preemptive(n, arrival, burst, execution);
}

/**
 * Shortest Job First (SJF) Scheduling Algorithm
 * Non-preemptive algorithm that selects the process with shortest burst time
 * @param n: Number of processes
 * @param pid: Array of process IDs
 * @param arrival: Array of arrival times
 * @param burst: Array of burst times
 */
void SJF(int n, int pid[], int arrival[], int burst[]) {
    printf("SJF Scheduling Algorithm =>\n");
    int execution[n];
    int check[n];
    int current_time = 0;

    // Initialize check array
    for (int i = 0; i < n; i++) {
        check[i] = 0;
    }

    // Determine execution order based on shortest burst time
    for (int i = 0; i < n; i++) {
        int a = min_index_sjf(arrival, burst, n, check, current_time);
        if (a != -1) {
            execution[i] = pid[a];
            check[a] = 1;
            // Update current time to when this process would complete
            if (current_time < arrival[a]) {
                current_time = arrival[a]; // Wait for process to arrive
            }
            current_time += burst[a];
        }
    }

    // Display execution order
    printf("Execution order: ");
    for (int i = 0; i < n; i++) {
        printf("%d ", execution[i]);
    }
    printf("\n");

    calculate_times_non_preemptive(n, arrival, burst, execution);
}

/**
 * Shortest Remaining Time First (SRTF) Scheduling Algorithm
 * Preemptive version of SJF - can interrupt currently running process
 * @param n: Number of processes
 * @param pid: Array of process IDs
 * @param arrival: Array of arrival times
 * @param burst: Array of burst times
 */
void SRTF(int n, int pid[], int arrival[], int burst[]) {
    printf("SRTF Scheduling Algorithm =>\n");

    int remaining_burst[n];
    int check[n];
    int start_time[n];
    int end_time[n];
    int current_time = 0;
    int completed = 0;

    // Initialize arrays
    for (int i = 0; i < n; i++) {
        remaining_burst[i] = burst[i];
        check[i] = 0;
        start_time[i] = -1; // -1 indicates process hasn't started
        end_time[i] = -1;
    }

    printf("Execution order: ");
    
    // Continue until all processes are completed
    while (completed < n) {
        int min_index = -1;
        int min_burst = INT_MAX;

        // Find process with shortest remaining time among arrived processes
        for (int i = 0; i < n; i++) {
            if (arrival[i] <= current_time && !check[i] && remaining_burst[i] < min_burst) {
                min_burst = remaining_burst[i];
                min_index = i;
            }
        }

        if (min_index != -1) {
            printf("%d ", pid[min_index]);

            // Record start time if this is the first execution
            if (start_time[min_index] == -1) {
                start_time[min_index] = current_time;
            }

            // Execute for 1 time unit
            remaining_burst[min_index]--;
            current_time++;

            // Check if process is completed
            if (remaining_burst[min_index] == 0) {
                check[min_index] = 1;
                end_time[min_index] = current_time;
                completed++;
            }
        } else {
            // No process available, advance time
            current_time++;
        }
    }
    printf("\n");

    calculate_times_preemptive(n, arrival, start_time, end_time);
}

/**
 * Round Robin Scheduling Algorithm
 * Preemptive algorithm that gives each process a fixed time quantum
 * @param n: Number of processes
 * @param pid: Array of process IDs
 * @param arrival: Array of arrival times
 * @param burst: Array of burst times
 * @param quantum: Time quantum for each process
 */
void RoundRobin(int n, int pid[], int arrival[], int burst[], int quantum) {
    printf("Round Robin Scheduling Algorithm =>\n");

    int remaining_burst[n];
    int check[n];
    int start_time[n];
    int end_time[n];
    int current_time = 0;
    int completed = 0;
    
    // Queue to maintain round robin order
    int queue[1000]; // Assuming reasonable limit
    int front = 0, rear = 0;
    int in_queue[n]; // Track which processes are in queue

    // Initialize arrays
    for (int i = 0; i < n; i++) {
        remaining_burst[i] = burst[i];
        check[i] = 0;
        start_time[i] = -1;
        end_time[i] = -1;
        in_queue[i] = 0;
    }

    // Add processes that arrive at time 0 to queue
    for (int i = 0; i < n; i++) {
        if (arrival[i] == 0) {
            queue[rear++] = i;
            in_queue[i] = 1;
        }
    }

    printf("Execution sequence: ");

    // Continue until all processes are completed
    while (completed < n) {
        if (front == rear) {
            // Queue is empty, find next arriving process
            int next_arrival_time = INT_MAX;
            for (int i = 0; i < n; i++) {
                if (!check[i] && arrival[i] > current_time && arrival[i] < next_arrival_time) {
                    next_arrival_time = arrival[i];
                }
            }
            if (next_arrival_time != INT_MAX) {
                current_time = next_arrival_time;
                // Add all processes that arrive at this time
                for (int i = 0; i < n; i++) {
                    if (arrival[i] == current_time && !check[i] && !in_queue[i]) {
                        queue[rear++] = i;
                        in_queue[i] = 1;
                    }
                }
            }
        }
        
        if (front < rear) {
            int i = queue[front++];
            in_queue[i] = 0;
            
            // Record start time if this is the first execution
            if (start_time[i] == -1) {
                start_time[i] = current_time;
            }

            // Calculate time slice (minimum of quantum and remaining burst)
            int time_slice = (remaining_burst[i] < quantum) ? remaining_burst[i] : quantum;
            remaining_burst[i] -= time_slice;
            current_time += time_slice;

            printf("%d ", pid[i]);

            // Add newly arrived processes to queue
            for (int j = 0; j < n; j++) {
                if (arrival[j] <= current_time && !check[j] && !in_queue[j] && j != i) {
                    queue[rear++] = j;
                    in_queue[j] = 1;
                }
            }

            // Check if process is completed
            if (remaining_burst[i] == 0) {
                check[i] = 1;
                end_time[i] = current_time;
                completed++;
            } else {
                // Add back to queue if not completed
                queue[rear++] = i;
                in_queue[i] = 1;
            }
        }
    }
    printf("\n");
    
    calculate_times_preemptive(n, arrival, start_time, end_time);
}

/**
 * Main function - Entry point of the program
 * Handles input validation and calls all scheduling algorithms
 */
int main() {
    int n;
    printf("Number of Processes: ");
    scanf("%d", &n);

    // Validate number of processes
    if (n <= 4) {
        printf("ERROR: Processes should be more than 4\n");
        return 1;
    }

    int pid[n];
    int arrival[n];
    int burst[n];

    // Input process details with validation
    printf("Enter details for each process (PID, Arrival, Burst):\n");
    for (int i = 0; i < n; i++) {
        printf("Process %d: ", i + 1);
        scanf("%d %d %d", &pid[i], &arrival[i], &burst[i]);
        
        // Validate input values
        if (pid[i] < 0 || arrival[i] < 0 || burst[i] <= 0) {
            printf("ERROR: Invalid arguments. PID and arrival time must be non-negative, burst time must be positive.\n");
            return 1;
        }
    }

    int quantum;
    printf("Enter Time Quantum: ");
    scanf("%d", &quantum);

    // Validate time quantum
    if (quantum <= 0) {
        printf("ERROR: Quantum should be more than 0\n");
        return 1;
    }

    // Execute all scheduling algorithms
    FIFO(n, pid, arrival, burst);
    SJF(n, pid, arrival, burst);
    SRTF(n, pid, arrival, burst);
    RoundRobin(n, pid, arrival, burst, quantum);

    return 0;
}
