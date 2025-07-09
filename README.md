# Multi-Train Simulator (MTS)

A C-based personal project that models an automated railway control system with multiple trains sharing a single main track. The simulator demonstrates key concepts of concurrent programming, scheduling, and synchronization using POSIX threads.

---

## Overview

* **Purpose**: Emulate an automated railway dispatch system where trains load at two stations, wait for clearance, and cross a single-track segment without collisions.
* **Key Concepts**:

  * Concurrency with threads
  * Mutual exclusion and condition synchronization
  * Priority-based dispatch and starvation avoidance
  * Timestamped event logging

---

## Features

* **Threaded Simulation**: Each train is represented by a thread that loads, waits, crosses, and exits.
* **Priority Scheduling**: High-priority trains (uppercase direction) always dispatch before low-priority.
* **Tie-breaking Logic**:

  1. Same direction & priority: earliest-loaded first.
  2. Opposite directions: alternate from last crossing direction.
* **Starvation Avoidance**: After two consecutive crossings in the same direction, forces one crossing from the opposite side.
* **Timestamped Output**: Logs events in `HH:MM:SS.t` format, showing tenths of a second since simulation start.

---

## Build & Run

1. **Clone or copy** this repository:

   ```bash
   git clone <your-repo-url>
   cd <repo-folder>
   ```
2. **Build** with the Makefile:

   ```bash
   make mts
   ```
3. **Run** with an input file of train definitions:

   ```bash
   ./mts trains.txt
   ```

---

## Input Format

Each line in the input file describes one train:

```
<direction> <load_time> <cross_time>
```

* `<direction>`: `e`, `E`, `w`, or `W` (`e`/`w` = low-priority, `E`/`W` = high-priority; lowercase = east/west-bound).
* `<load_time>` and `<cross_time>`: integers from 1 to 99, representing tenths of a second.

Trains are automatically numbered 0, 1, 2, ... in file order.

---

## Output Events

For each train, three timestamped messages are printed in order:

1. **Ready** (after loading):

   ```
   HH:MM:SS.t Train  N is ready to go DIRECTION
   ```
2. **Crossing** (entering main track):

   ```
   HH:MM:SS.t Train  N is ON the main track going DIRECTION
   ```
3. **Finished** (after crossing):

   ```
   HH:MM:SS.t Train  N is OFF the main track after going DIRECTION
   ```

`N` = train ID, `DIRECTION` = `East` or `West`.

---

## Design Highlights

* **Data Structures**: Two linked lists track "ready" trains for eastbound and westbound.
* **Synchronization**:

  * A global mutex guards queue and track state.
  * Individual condition variables wake trains when cleared to cross.
  * Broadcast signals ensure the scheduler reacts to new readiness or track availability.
* **Scheduler Logic**: Runs in the main thread, selecting one train at a time based on priority, load-completion time, direction alternation, and anti-starvation rules.

---
