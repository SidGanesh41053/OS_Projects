# CS537 Fall 2024, Project 4

In this assignment, we implemented dynamic stride scheduler within xv6.

---

## My Information (and my partner's information)

### Me
- **Name**: Sid Ganesh
- **CS Login**: sganesh
- **Wisc ID and Email**: sganesh9@wisc.edu
- **Status of Implementation**: Working as intended, passing all test cases

### My Partner
- **Name**: Rithwik Babu
- **CS Login**: rithwik
- **Wisc ID and Email**: rbabu4@wisc.edu
- **Status of Implementation**: Working as intended, passing all test cases

---

## Implementation Description
### Comparison of Scheduling in RR vs. Stride Scheduling
Round Robin (RR) Scheduling:
- In RR, each process receives equal CPU time regardless of ticket count -> uniform runtime growth across processes -> inefficiencies b/c processes with different priorities (ticket counts) receive the same CPU share.

Stride Scheduling:
1. Adjusts CPU time based on each process's ticket count. Processes with more tickets (indicating higher priority) are scheduled more frequently due to their smaller strides, which keeps their "pass" values low and ensures they are ready for scheduling sooner.
2. Ensures that processes with more tickets receive more CPU time, aligning with fair distribution policies.
3. High-ticket processes avoid starvation, as their lower strides prioritize them in the scheduling queue.
4. Processes that leave or are no longer in the RUNNABLE (or equivalent READY) state have a remain calculation that essentially acts as a checkpoint to catch them up if they return to the scheduler, and new processes coming to the scheduler are set to the global pass (which is why the global variables are important as they act as an average)

### Behavior and Patterns of Process Runtimes with Dynamic Participation
RR Scheduling:
- Processes in RR receive equal CPU time without considering priority -> inefficient resource utilization for high-priority tasks.

Stride Scheduling:
- Stride Scheduling shows non-linear runtime growth, where high-ticket processes gain runtime faster than low-ticket processes. Adjustments in ticket counts or process arrivals dynamically shift the global stride and ticket allocation so that CPU time aligns with active priorities.

### Turnaround and Response Times
- Turnaround Time: Stride Scheduling generally improves turnaround times for high-priority tasks, as they receive more frequent CPU access. Low-ticket processes also benefit as system resources are redistributed dynamically.
- Response Time: High-ticket processes receive faster response times due to their lower strides.

### Fairness Condition Analysis
The condition pass(A) - pass(B) <= max(stride(A), stride(B)) generally holds true throughout the simulation, ensuring fairness across processes by maintaining a balanced distribution of CPU time relative to ticket counts. The condition may not hold strictly in the first two initial phases of each epoch. For the first two scheduled processes, the equation can be briefly violated. Changes in process participation, such as new arrivals or departures, lead to adjustments in global tickets and strides, creating temporary deviations from the fairness equation until the scheduling stabilizes.

## Final
Overall, our dynamic stride scheduler functions as expected and the outputs for the CSV files for RR and STRIDE were checked over and determined as correct by multiple TAs. Also, all of our test cases pass, and the equation above generally holds. Furthermore, even though the order of processes itself is deterministic, because of the nature of getpinfo, outputs from multiple runs of workload in qemu-nox could be different and non-deterministic, which is something we experienced all the different times we ran it. However, the proportionality relationship between tickets and runtime is direct (which we observed), and the proportionality relationship between stride and runtime is inversely related (which is expected in how it's calculated and which we observed as well).
