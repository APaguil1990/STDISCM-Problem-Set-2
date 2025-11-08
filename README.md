# LFG (Looking for Group) Dungeon Queuing System 

## Overview 
A C++20 multithreaded dungeon queuing system for MMORPGs that manages party formation and instance allocation with proper synchronization. The system ensures fair distribution of players across available dungeon instances while preventing deadlocks and starvation. 

## Objectives 
- **Efficient Party Formation**: Create parties of 1 tank, 1 healer, and 3 DPS players 
- **Instance Management**: Manage multiple concurrent dungeon instances 
- **Synchronization**: Prevent deadlocks and starvation using modern C++20 features 
- **Fair Distribution**: Ensure all instances get equal opportunities to serve parties 
- **Real-time Monitoring**: Provide comprehensive logging with timestamps 

## Quick Start 
- Compile: **g++ -std=c++20 -O3 -pthread LookingForGroup.cpp -o lfg_test.exe** 
- Execute: **lfg_test** 

## User Input Mechanism 
The program accepts the following inputs interactively: 

1. **Maximum Instances (n)**: Number of concurrent dungeon instances (must be a positive integer)
2. **Tank Players (t)**: Number of tanks in queue (must be a non-negative integer) 
3. **Healer Players (h)**: Number of healers in queue (must be a non-negative integer) 
4. **DPS Players**: Number of DPS in queue (must be a non-negative integer) 
5. **Minmimum Clear Time (t1)**: Fastest dungeon completion time in seconds (1-15) 
6. **Maximum Clear Time (t2)**: Slowest dungeon completion time in seconds (1-15, >= t1)

## Input Validation 
- **All numeric inputs** must be non-negative 
- **Instance count** must be positive 
- **Clear times** must be between 1-15 seconds 
- **Maximum time** must be >= minimum time 

## Test Cases 
### **Test Case 1: Even-Steven (Balanced)** 
- Tanks: 10 
- Healers: 10 
- DPS: 30 

**Expected Behavior**: Forms 10 perfect parties (utilizes all players) 
**Output Pattern**: All instances serve parties, 100% distribution fairness 
**Remaining Players**: None

### **Test Case 2: No Healers** 
- Tanks: 20
- Healers: 5
- DPS: 30

**Expected Behavior**: Forms only 5 parties (limited by healers) 
**Output Pattern**: System identifies "No more healers available"
**Remaining Players**: 15 tanks, 35 DPS

### **Test Case 3: No Tanks** 
- Tanks: 10
- Healers: 50
- DPS: 100

**Expected Behavior**: Froms 10 parties (limited by tanks)
**Output Pattern**: System identifes "No more tanks available"
**Remaining Players**: 40 healers, 70 DPS

### **Test Case 4: Large-Scale Test** 
- Tanks: 200 
- Healers: 200
- DPS: 1000

**Expected Behavior**: Forms 200 parties (limited by tanks & Healers)
**Output Pattern**: System will eventually identify "No more tanks available" & "No more healers available"
**Remaining Players**: 400 DPS

## Demo Video Link 
- https://drive.google.com/file/d/1EB2dJQ-P853cS2Fd7s9qWCIx9YzdLrRB/view?usp=sharing 