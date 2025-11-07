# LFG (Looking for Group) Dungeon Queuing System 

## Overview 
A C++20 multithreaded dungeon queuing system for MMORPGs that manages party formation and instance allocation with proper synchronization. The system ensures fair distribution of players across available dungeon instances while preventing deadlocks and starvation. 

## Objectives 
- **Efficient Party Formation**: Create parties of 1 tank, 1 healer, and 3 DPS players 
- **Instance Management**: Manage multiple concurrent dungeon instances 
- **Synchronization**: Prevent deadlocks and starvation using modern C++20 features 
- **Fair Distribution**: Ensure all instances get equal opportunities to serve parties 
- **Real-time Monitoring**: Provide comprehensive logging with timestamps