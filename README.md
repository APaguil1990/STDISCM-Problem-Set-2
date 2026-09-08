# LFG (Looking for Group) Dungeon Queuing System 

## Overview 
A C++20 multithreaded dungeon queuing simulation for MMORPG-style party formation and dungeon instance processing. 

The implmentation separates user interaction from simulation logic, uses condition variables instead of polling, protects shared state with well-defined mutexes, gives each dungeon worker its own random-number generator, and manages worker threads through RAII and a clean shutdown logic. 

A valid party always contains:
- **1 Tank** 
- **1 Healer**
- **3 DPS**

The theoretical maximum number of parties is:
```c++
std::min({tanks, healers, dps / 3});
```

When all players are supplied before processing begins, the system forms exactly this many complete parties. 

## Objectives 
- **Efficient Party Formation**: Atomically claim exactly 1 Tank, 1 Healer, and 3 DPS per party
- **Concurrent Instance Processing**: Allow multiple dungeon instances to execute independently
- **Condition-Variable Scheduling**: Let workers sleep while no work is available instead of polling
- **Safe Randomization**: Give each dungeon instance its own `std::mt19937`
- **RAII Thread Management**: Ensure every started worker thread is eventually joined
- **Clean Shutdown**: Support safe and idempotent shutdown behavior
- **Input Validation**: Reject invalid, out-of-range, and non-numeric user input
- **Accurate Statistics**: Report parties formed, per-instance work, dungeon time, remaining players, and bottlenecks
- **Maintainable Design**: Separate public interfaces, implementation details, and user-interfacing program flow

## Project Structure 
```text
Problem Set 2/
├──.vscode
    ├── c_cpp_properties.json
    └── settings.json
├── main.cpp
├── LookingForGroup.hpp
├── LookingForGroup.cpp
└── README.md
```

### File Responsibilities
- **`main.cpp`**
  - Reads and validates user input 
  - Builds the simulation configuration
  - Starts and waits for the LFG system
  - Requests shutdown
  - Prints the final summary and bottleneck information

- **`LookingForGroup.hpp`**
  - Declares the public LFG interface
  - Defines configuration, player-count, status, and summary structures
  - Hides worker and logging implementation details
  
- **`LookingForGroup.cpp`**
  - Implements party formation
  - Manages dungeon worker threads
  - Handles synchronization and condition variables
  - Impleemnts thread-safe logging
  - Generates per-instance dungeon clear times
  - Tracks simulation statistics and shutdown state

## Quick Start 
### Compile
```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread main.cpp LookingForGroup.cpp -o lfg.exe
```

# Execute
Windows:
```bash
lfg.exe
```

Linux/macOS:
```bash
./lfg
```

The warning flags are intentionally enabled:
- `-Wall` enables a broad set of useful compiler warnings
- `-Wextra` enables additional diagnostics
- `-Wpedantic` warns about code that relies on non-standard compiler extensions

## User Input Mechanism 
The program accepts the following values interactively: 

1. **Maximum Concurrent Instances**: Number of dungeon worker instances
2. **Tank Players**: Number of Tanks in the initial queue
3. **Healer Players**: Number of Healers in the initial queue
4. **DPS Players**: Number of DPS players in the initial queue
5. **Minimum Clear Time**: Minimum simulated dungeon duration in seconds
6. **Maximum Clear Time**: Maximum simulated dungeon duration in seconds

Example:
```text
Maximum concurrent instances: 3
Tank players: 10
Healers players: 10
DPS players: 30
Minimum dungeon clear time in seconds: 1
Maximum dungeon clear time in seconds: 3
```

## Input Validation 
The following rules are enforced: 

```text
maxInstances > 0
tanks >= 0
healers >= 0
dps >= 0
1 <= minClearTime <= 15
1 <= maxClearTime <= 15
maxClearTime >= minClearTime
```

The program also handles non-numeric input safely.

For example:
```text
Tank players: hello
Invalid input. Please enter a whole number.
```

Input s read one line at a time so a failed extraction does not leave `std::cin` in an unusable state.

## Architecture
```text
main.cpp
   | 
   v
LFGSystem
   |
   +-- shared waiting-player counts
   +-- simulation lifecycle
   +-- condition variables
   +-- completion tracking
   +-- statistics
   |
   +-- DungeonInstance 1 --> worker thread + private RNG
   +-- DungeonInstance 2 --> worker thread + private RNG
   +-- DungeonInstance N --> worker thread + private RNG
   | 
   +-- ThreadSafeLogger --> synchronized console output
```

Each `DungeonInstance` owns one worker thread and one private random-number engine

The LFG system owns all instances and coordinates access to shared simulation state. 

## Concurrency Model
Each dungeon instance instance executes in its own `std::thread`.

With three instances and enough players, execution can conceptually look like:

```text
Instance 1 -> claim Party 1 -> run dungeon
Instance 2 -> claim Party 2 -> run dungeon
Instance 3 -> claim Party 3 -> run dungeon
```

Party claiming is synchronized, but dungeon execution is not serialized behind the queue mutex.. 

A worker releases te shared-state mutex before:
```c++
std::this_thread::sleep_for(std::chrono::seconds(dungeonSeconds));
```

This allows multiple dungeon simulations to overlap concurrently and, when scheduled on separate CPU cores, execute in parallel.

## Synchronization Model 
### Shared State Mutex
`stateMutex_` protects shared mutable simulation state, including:
- Remaining Tank count
- Remaining Healer count
- Remaining DPS count
- Total parties formed
- Number of active dungeon runs
- Instance status 
- Per-instance statistics
- Start/shutdown state used by workers

### Lifecycle Mutex
`lifecycleMutex_` serializes lifecycle operations such as:
- `start()`
- `shutdown()`

This prevents concurrent start/shutdown operations from corrupting worker-thread lifecycle state. 

### Work Condition Variable
`workAvailableCv_` allows workers to sleep unti:
```text
a complete party can be formed
OR 
shutdown has been requested
```

Its effective predicate is:
```c++
shutdownRequested_ || canFormPartyLocked()
```

This handles spurious wakeups correctly and removes the need for polling delays.

### Completion Condition Variable
`completionCv_` allows the main thread to wait efficiently until:

```text
all theoretically possible parties have been claimed
AND
all active dungeon runs have finished
```

It can also wake when shutdown has been requested and no active runs remain.

## Atomic Party Formation
A party can be claimed only while the shared mutex is held.

The condition is:
```text
Tanks >= 1
Healers >= 1
DPS >= 3
``` 

A successful claim performs:
```text
Tanks   -= 1
Healers -= 1
DPS     -= 3
```

as one protected operation.

This guarantees that:
- A player cannot belong to two parties
- Two workers cannot claim the same players
- Player counts cannot become partially updated during party formation
- Player counts cannot become negative when the party predicate is satisfied

## Dungeon Instance Lifecycle
Each instance transitions through strongly typed states:
```text
Idle
  |
  v
Waiting
  |
  v
Running
  |
  v
Idle
```

When shutdown is requested:
```text
Waiting / Idle
      |
      v
    Stopped
```

Each instance processes at most one dungeon party at a time.

## Random Number Generation
Each dungeon worker owns its own `std::mt19937` engine.

This avoids sharing one mutable random-number generator between worker threads and therefore avoids an RNG data race without introducing an additional mutex.

Dungeon clear times are generated independently within the configured range:
```text
minClearTime <= dungeonTime <= maxClearTime
```

## Thread-Safe Logging
Logging is centralized through `ThreadSafeLogger`.

A dedicated output mutex ensures that messages from multiple worker threads do not interleave or corrupt one another.

Logs include milliseconds-resolution timestamps, for example:
```text
[14:32:15.153] Instance 2 formed party 4. Remaining - Tanks: 6, Healers: 6, DPS: 18
[14:32:15.154] Instance 2 starting dungeon for party 4 (estimated time: 3s)
```

## Shutdown and RAII
The simulation owns all worker threads.
- No threads are detached
- Every started worker is join 
- `DungeonInstance` joins its thread in its destructor as a final RAII safeguard
- `LFGSystem::~LFGSystem()` calls `shutdown()` 
- `shutdown()` is designed to be safe when called more than once
- Active dungeon runs are allowed to finish before their threads exit


This prevents destruction of joinable `std::thread` objects, which would otherwise call `std::terminate()`.

## Fairness
Party assignment is intentionally **scheduler-dependent**.

Workers compete for the shared queue when a complete party is available. The design provides reasonable concurrent scheduling but does **not** claim strict round-robin fairness.

For example, with three instances and ten parties, a possible distribution is:

```text
Instance 1: 4 parties
Instance 2: 3 parties
Instance 3: 3 parties
``` 

Another valid run may produce a different distribution because of operating-system scheduling and randomly generated dungeon durations. 

The final summary reports:
```text
Party distribution spread (max-min)
```

to show the difference between the busiest and least-used instance.

A spread of `0` means all instances served the same number of parties, but this is not guaranteed.

## Output Features
- **Timestamped Worker Logs**: Thread-safe party formation and dungeon activity messages
- **Maximum Party Calculation**: Shows the theoretircal number of complete parties before execution
- **Party Formation Logs**: Shows which instance claimed each party
- **Remaining Queue Counts**: Shows Tanks, Healers, and DPS after each claim
- **Random Dungeon Duration**: Each dungeon uses a random configured clear time
- **Per-Instance Statistics**: 
  - Final status
  - Parties served
  - Total simulated dungeon time
- **Distribution Spread**: Reports `maximum parties served - minimum parties served`
- **Remaining Players**: Reports all unused players
- **Bottleneck Detection**: Reports every role preventing another complete party

## Bottleneck Detection
The program can report multiple limiting roles at the same time. 

Examples: 
```text
No more tanks are available.
No more healers are available.
```

or

```text
Fewer than 3 DPS players remain.
```

## Test Cases 
The following test cases verify balanced queues, role bottlenecks, empty roles, more workers than parties, and larger workloads.

### Test Case 1: Balanced Queue
```text
Instances: 3
Tanks:     10
Healers:   10
DPS:       30
Min Time:  1
Max Time:  3
```

**Expected Parties:** `10`  
**Expected Remaining Players:** `0 Tanks, 0 Healers, 0 DPS`

### Test Case 2: Healer Bottleneck

```text
Instances: 5
Tanks:     20
Healers:   5
DPS:       30
Min Time:  1
Max Time:  3
```

**Expected Parties:** `5`  
**Expected Remaining Players:** `15 Tanks, 0 Healers, 15 DPS`  
**Primary Bottleneck:** Healers

### Test Case 3: Tank Bottleneck

```text
Instances: 4
Tanks:     10
Healers:   50
DPS:       100
Min Time:  1
Max Time:  3
```

**Expected Parties:** `10`  
**Expected Remaining Players:** `0 Tanks, 40 Healers, 70 DPS`  
**Primary Bottleneck:** Tanks

### Test Case 4: Large Queue

```text
Instances: 10
Tanks:     200
Healers:   200
DPS:       1000
Min Time:  1
Max Time:  2
```

**Expected Parties:** `200`  
**Expected Remaining Players:** `0 Tanks, 0 Healers, 400 DPS`  
**Bottlenecks:** Tanks and Healers

### Test Case 5: DPS Bottleneck

```text
Instances: 3
Tanks:     10
Healers:   10
DPS:       11
Min Time:  1
Max Time:  3
```

**Expected Parties:** `3`  
**Expected Remaining Players:** `7 Tanks, 7 Healers, 2 DPS`  
**Primary Bottleneck:** DPS

### Test Case 6: No Possible Party

```text
Instances: 5
Tanks:     0
Healers:   10
DPS:       30
Min Time:  1
Max Time:  2
```

**Expected Parties:** `0`  
**Expected Remaining Players:** `0 Tanks, 10 Healers, 30 DPS`

The application should complete and shut down normally without hanging.

### Test Case 7: More Instances Than Parties

```text
Instances: 20
Tanks:     2
Healers:   2
DPS:       6
Min Time:  1
Max Time:  2
```

**Expected Parties:** `2`  
**Expected Remaining Players:** `0 Tanks, 0 Healers, 0 DPS`

Unused worker threads must still terminate cleanly.

### Test Case 8: Not Enough DPS for One Party

```text
Instances: 4
Tanks:     10
Healers:   10
DPS:       2
Min Time:  1
Max Time:  3
```

**Expected Parties:** `0`  
**Expected Remaining Players:** `10 Tanks, 10 Healers, 2 DPS`

### Test Case 9: Exactly One Party

```text
Instances: 1
Tanks:     1
Healers:   1
DPS:       3
Min Time:  1
Max Time:  1
```

**Expected Parties:** `1`  
**Expected Remaining Players:** `0 Tanks, 0 Healers, 0 DPS`

### Test Case 10: Uneven Resources

```text
Instances: 5
Tanks:     8
Healers:   12
DPS:       20
Min Time:  1
Max Time:  4
```

DPS can support only:

```text
20 / 3 = 6 parties
```

**Expected Parties:** `6`  
**Expected Remaining Players:** `2 Tanks, 6 Healers, 2 DPS`

## Invalid Input Tests

The input layer should reject and re-prompt for values such as:

### Invalid Instance Count

```text
Maximum concurrent instances: 0
```

Expected:

```text
Invalid value. The instance count must be greater than 0.
```

### Negative Player Count

```text
Tank players: -5
```

Expected:

```text
Invalid value. The tank count must be non-negative.
```

### Non-Numeric Input

```text
DPS players: hello
```

Expected:

```text
Invalid input. Please enter a whole number.
```

### Clear Time Below Minimum

```text
Minimum dungeon clear time in seconds: 0
```

Expected rejection because clear times must be between `1` and `15`.

### Clear Time Above Maximum

```text
Maximum dungeon clear time in seconds: 20
```

Expected rejection because clear times must be between `1` and `15`.

### Maximum Time Less Than Minimum

```text
Minimum dungeon clear time in seconds: 5
Maximum dungeon clear time in seconds: 3
```

Expected:

```text
Invalid value. Maximum clear time must be >= minimum clear time.
```

## Synchronization Guarantees
The implementation is designed around the following guarantees:
- **Atomic Party Claims**: Exactly 1 Tank, 1 Healer, and 3 DPS are removed together
- **No Duplicate Player Claims**: Shared player counts are protected by `stateMutex_`
- **No Dungeon Sleep Under Shared-State Lock**: Workers release the mutex before simulating a dungeon
- **Condition-Variable Waiting**: Workers do not repeatedly poll for available work
- **Thread-Safe Statistics**: Shared counters and instance statistics are protected consistently
- **Thread-Safe RNG Ownership**: Each worker has its own random engine
- **Thread-Safe Logging**: Console output is serialized through a dedicated logger mutex
- **Clean Worker Termination**: Shutdown wakes blocked workers and joins every thread
- **RAII Protection**: Destructors prevent joinable worker threads from being abandoned
- **Simple Locking Model**: Shared state and lifecycle operations use separate, clearly scoped mutexes

The implementation does not rely on timeout-based synchronization or artificial sleep delays to create fairness.

## Key Algorithms
1. **Maximum Party Calculation**
   ```cpp
   std::min({tanks, healers, dps / 3});
   ```

2. **Atomic Party Claim**
   ```text
   Tank   -= 1
   Healer -= 1
   DPS    -= 3
   ```

3. **Worker Scheduling**
   ```text
   Wait on condition variable
        |
        +-- party available -> claim and run
        |
        +-- shutdown requested -> stop
   ```

4. **Completion Detection**
   ```text
   all possible parties claimed
   AND
   active dungeon runs == 0
   ```

5. **Bottleneck Detection**
   - Tanks `< 1`
   - Healers `< 1`
   - DPS `< 3`

## Important Correctness Invariants
During normal simulation execution:
- A successful party claim always consumes exactly `1 Tank`, `1 Healer`, and `3 DPS`
- Player counts never become negative
- Two workers cannot claim the same players
- An instance processes at most one party at a time
- Shared mutable simulation state is accessed under the appropriate mutex
- No worker holds `stateMutex_` during dungeon simulation
- Every started worker thread is eventually joined
- The total number of formed parties never exceeds `maximumPossibleParties`
- When all initial players are submitted before processing, the final party count equals `maximumPossibleParties`
- Party-to-instance distribution may vary between runs

## Summary 
The refactored LFG system demonstrates:
- C++20 object-oriented design
- Multithreading and concurrent dungeon execution
- Condition-variable synchronization
- Atomic party formation
- Thread-safe shared state
- Per-worker random-number generation
- RAII thread ownership
- Robust user input validation
- Idempotent shutdown behavior
- Accurate remaining-player calculations
- Scheduler-dependent instance distribution reporting
- Multi-role bottleneck detection

The implementation prioritizes correctness, understandable, synchronization, and maintainability over artificial fairness mechanisms or unnecessary abstraction.

## Developed By:
- James Archer B. Paguiligan

## Demo Video Link 
- https://drive.google.com/file/d/1EB2dJQ-P853cS2Fd7s9qWCIx9YzdLrRB/view?usp=sharing 