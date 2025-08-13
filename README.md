# CPU Scheduling Algorithms Implementation

A comprehensive implementation of four fundamental CPU scheduling algorithms in C programming language.

## Overview

This project implements and compares four different CPU scheduling algorithms:

- **FIFO (First In First Out)** - Non-preemptive
- **SJF (Shortest Job First)** - Non-preemptive  
- **SRTF (Shortest Remaining Time First)** - Preemptive
- **Round Robin (RR)** - Preemptive with time quantum

## Features

- Complete implementation of all four major scheduling algorithms
- Performance metrics calculation (average response time and turnaround time)
- Comprehensive input validation and error checking
- Detailed execution output with timing statistics
- Extensive code documentation and comments

## Tech Stack

- **Language**: C
- **Compiler**: GCC (or any C99 compatible compiler)
- **Platform**: Cross-platform (Linux, macOS, Windows)

## Installation and Setup

### Prerequisites
- C compiler (GCC recommended)
- Standard C library

### Compilation

```bash
# Basic compilation
gcc -o cpu_scheduling cpu_scheduling.c

# With additional warnings (recommended)
gcc -Wall -Wextra -o cpu_scheduling cpu_scheduling.c

# For debugging
gcc -g -Wall -o cpu_scheduling cpu_scheduling.c
```

## Usage

### Running the Program

```bash
./cpu_scheduling
```

### Input Requirements

The program requires the following inputs:

1. **Number of Processes**: Must be greater than 4
2. **Process Details**: For each process, provide:
   - Process ID (PID) - Non-negative integer
   - Arrival Time - Non-negative integer
   - Burst Time - Positive integer
3. **Time Quantum**: Positive integer for Round Robin scheduling

### Example Usage

```
Number of Processes: 5
Enter details for each process (PID, Arrival, Burst):
Process 1: 1 0 5
Process 2: 2 1 3
Process 3: 3 2 8
Process 4: 4 3 6
Process 5: 5 4 4
Enter Time Quantum: 2
```

### Sample Output

```
FIFO Scheduling Algorithm =>
Execution order: 1 2 3 4 5
Average Response Time: 6.00
Average Turnaround Time: 11.20

SJF Scheduling Algorithm =>
Execution order: 1 2 5 4 3
Average Response Time: 4.40
Average Turnaround Time: 9.60

SRTF Scheduling Algorithm =>
Execution order: 1 2 2 5 5 4 4 4 4 4 3 3 3 3 3 3 3 3
Average Response Time: 2.20
Average Turnaround Time: 9.00

Round Robin Scheduling Algorithm =>
Execution sequence: 1 2 1 3 2 5 1 4 3 5 4 3 4 3 4 3 3
Average Response Time: 2.20
Average Turnaround Time: 9.00
```

## Algorithm Specifications

### First In First Out (FIFO)
- **Type**: Non-preemptive
- **Logic**: Executes processes in order of arrival time
- **Advantages**: Simple implementation, fair for processes with similar burst times
- **Disadvantages**: Poor average waiting time, convoy effect with long processes

### Shortest Job First (SJF)
- **Type**: Non-preemptive
- **Logic**: Selects process with shortest burst time among arrived processes
- **Advantages**: Optimal average waiting time for non-preemptive algorithms
- **Disadvantages**: Potential starvation of long processes, requires burst time prediction

### Shortest Remaining Time First (SRTF)
- **Type**: Preemptive
- **Logic**: Always executes process with shortest remaining execution time
- **Advantages**: Optimal average waiting time among all algorithms
- **Disadvantages**: High context switching overhead, potential starvation

### Round Robin
- **Type**: Preemptive
- **Logic**: Each process receives equal time quantum in circular order
- **Advantages**: Fair scheduling, good response time, prevents starvation
- **Disadvantages**: Performance heavily dependent on time quantum selection

## Performance Metrics

The implementation calculates two key performance indicators:

- **Response Time**: Time interval from process arrival to first CPU execution
- **Turnaround Time**: Time interval from process arrival to completion

## Input Validation

The program validates all inputs according to these constraints:

- Number of processes must exceed 4
- Process IDs and arrival times must be non-negative
- Burst times must be positive integers
- Time quantum must be a positive integer

## Code Architecture

```
cpu_scheduling.c
├── Utility Functions
│   ├── min_index_fifo()              # Finds process with earliest arrival
│   ├── min_index_sjf()               # Finds process with shortest burst time
│   ├── calculate_times_non_preemptive()  # Metrics for FIFO and SJF
│   └── calculate_times_preemptive()      # Metrics for SRTF and Round Robin
├── Scheduling Algorithms
│   ├── FIFO()                        # First In First Out implementation
│   ├── SJF()                         # Shortest Job First implementation
│   ├── SRTF()                        # Shortest Remaining Time First implementation
│   └── RoundRobin()                  # Round Robin implementation
└── main()                            # Input handling and program execution
```

## Error Handling

| Error Message | Cause | Resolution |
|---------------|--------|------------|
| "Processes should be more than 4" | Process count ≤ 4 | Enter a value greater than 4 |
| "Invalid arguments" | Negative PID/arrival or non-positive burst time | Use valid non-negative values for PID/arrival and positive values for burst time |
| "Quantum should be more than 0" | Time quantum ≤ 0 | Enter a positive integer for time quantum |

## Testing

### Test Case 1: Standard Scenario
```
Number of Processes: 5
Process 1: 1 0 5
Process 2: 2 1 3  
Process 3: 3 2 8
Process 4: 4 3 6
Process 5: 5 4 4
Time Quantum: 2
```

### Test Case 2: Simultaneous Arrivals
```
Number of Processes: 5
Process 1: 1 0 4
Process 2: 2 0 2
Process 3: 3 0 6
Process 4: 4 0 3
Process 5: 5 0 1
Time Quantum: 1
```

## Build Configuration Options

```bash
# Optimized release build
gcc -O2 -o cpu_scheduling cpu_scheduling.c

# Debug build with symbols
gcc -g -DDEBUG -o cpu_scheduling cpu_scheduling.c

# Strict compilation with all warnings
gcc -Wall -Wextra -Wpedantic -std=c99 -o cpu_scheduling cpu_scheduling.c
```

## Educational Applications

This implementation serves multiple educational purposes:

- Operating Systems course assignments and projects
- Practical demonstration of CPU scheduling concepts
- Comparative analysis of algorithm performance characteristics
- C programming language learning and practice

## Development Guidelines

For contributors and developers:

1. Maintain consistent code formatting and style
2. Add comprehensive comments for new functions
3. Include input validation for all user inputs
4. Test with various process configurations
5. Document any algorithmic modifications

## Technical Requirements

- C compiler supporting C99 standard or later
- Minimum 1KB stack space for local variables
- Standard input/output capabilities
- Integer arithmetic support

## Limitations and Considerations

- Maximum process limit determined by system memory
- Time calculations use integer arithmetic (millisecond precision)
- Round Robin queue implementation uses fixed-size array
- No persistent storage of scheduling results

## Future Development

Potential enhancements for extended functionality:

- Priority-based scheduling algorithms
- Multilevel queue and multilevel feedback queue scheduling
- Graphical Gantt chart generation
- File-based input/output operations
- Statistical analysis and comparison tools

## License

This project is released under the MIT License.

## Support and Documentation

For technical support or questions:

1. Review the error handling section for common issues
2. Verify input format matches specified requirements
3. Ensure proper compilation with recommended flags
4. Consult example inputs and expected outputs

## Version History

- v1.0: Initial implementation with basic scheduling algorithms
- v1.1: Enhanced input validation and error handling
- v1.2: Improved Round Robin implementation with proper queue management
- v1.3: Corrected time calculation methods for accurate metrics

---

This implementation provides a solid foundation for understanding CPU scheduling algorithms and their comparative performance characteristics in operating system environments.
