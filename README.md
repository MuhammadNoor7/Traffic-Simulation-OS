# 🚦 OS Traffic Simulation Project

A comprehensive **multi-process, multi-threaded traffic simulation** built in C++ demonstrating core Operating System concepts including process management, thread synchronization, inter-process communication (IPC), and semaphore-based resource management.

> **Course**: Operating Systems  
> **Language**: C++17  
> **Platform**: Linux / WSL (Windows Subsystem for Linux)

---

## 📋 Table of Contents

1. [Project Overview](#-project-overview)
2. [What This Project Demonstrates](#-what-this-project-demonstrates)
3. [System Requirements](#-system-requirements)
4. [Complete Setup Guide](#-complete-setup-guide-step-by-step)
5. [Building the Project](#-building-the-project)
6. [Running the Simulation](#-running-the-simulation)
7. [Running the SFML Visualizer](#-running-the-sfml-visualizer)
8. [Implementation Status](#-implementation-status)
9. [Requirements Compliance](#-requirements-compliance)
10. [Project Architecture](#-project-architecture)
11. [Troubleshooting](#-troubleshooting)

---

## 🎯 Project Overview

This simulation models a real-world traffic system with:

- **Two Intersections** (F10 and F11) that communicate with each other
- **Six Vehicle Types**: Ambulance, Firetruck, Bus, Car, Bike, Tractor
- **Parking Lot** with limited spots and a waiting queue
- **Emergency Vehicle Preemption** - ambulances and firetrucks get priority
- **Visual Interface** using SFML graphics library

### The Scenario

Imagine two 4-way intersections connected by a road. Vehicles arrive randomly from different directions, wait their turn, cross safely (without collisions), and either:
- Exit the system
- Move to the other intersection (via IPC pipe)
- Park in the parking lot (if going to parking)

When an emergency vehicle (ambulance/firetruck) arrives, all other vehicles must wait until it passes!

---

## 🎓 What This Project Demonstrates

| OS Concept | Implementation |
|------------|----------------|
| **Process Creation** | `fork()` to create F10 and F11 controller processes |
| **Threads** | `pthread` for vehicle generator and vehicle threads |
| **Mutexes** | 4-quadrant intersection locking for safe crossing |
| **Semaphores** | Parking lot with 10 spots + bounded waiting queue |
| **Condition Variables** | Emergency mode blocking for normal vehicles |
| **Inter-Process Communication** | Bidirectional pipes between F10 ↔ F11 |
| **Signals** | `SIGINT` handling for graceful shutdown |

---

## 💻 System Requirements

### Operating System
- **Linux** (Ubuntu 20.04+ recommended)
- **Windows 10/11** with **WSL2** (Windows Subsystem for Linux)
- **macOS** (with some modifications)

### Hardware
- Any modern computer (no special requirements)
- For visualizer: Display capable of 1024x700 resolution

---

## 🚀 Complete Setup Guide (Step-by-Step)

### For Windows Users (WSL Setup)

If you're on Windows, you need WSL (Windows Subsystem for Linux). Here's how to set it up:

#### Step 1: Enable WSL

Open **PowerShell as Administrator** and run:

```powershell
wsl --install
```

This installs WSL2 with Ubuntu. **Restart your computer** when prompted.

#### Step 2: Set Up Ubuntu

After restart, Ubuntu will open automatically. Create a username and password when asked.

#### Step 3: Update Ubuntu

In the Ubuntu terminal, run:

```bash
sudo apt update && sudo apt upgrade -y
```

#### Step 4: Install Required Tools

```bash
# Install C++ compiler, make, and other build tools
sudo apt install build-essential -y

# Install SFML library for graphics
sudo apt install libsfml-dev -y

# Install git (if not already installed)
sudo apt install git -y
```

#### Step 5: Enable WSLg (for Graphics)

WSLg allows Linux GUI apps to display on Windows. It's included in WSL2 by default on Windows 11.

Check your WSL version:
```powershell
wsl --version
```

You should see something like:
```
WSL version: 2.x.x
WSLg version: 1.x.x
```

If WSLg version is missing, update WSL:
```powershell
wsl --update
```

---

### For Linux Users (Native Setup)

#### Ubuntu/Debian:
```bash
sudo apt update
sudo apt install build-essential libsfml-dev git -y
```

#### Fedora:
```bash
sudo dnf install gcc-c++ make SFML-devel git -y
```

#### Arch Linux:
```bash
sudo pacman -S base-devel sfml git
```

---

### Cloning the Project

```bash
# Navigate to where you want the project
cd ~

# Clone the repository
git clone https://github.com/Shahoud867/Traffic-Simulation-OS.git

# Enter the project directory
cd Traffic-Simulation-OS
```

---

## 🔨 Building the Project

### Quick Build (Recommended)

```bash
# Build everything (simulation + visualizer)
make all
```

### Individual Builds

```bash
# Build only the simulation (no graphics needed)
make sim

# Build only the visualizer (requires SFML)
make vis

# Clean all build files
make clean

# Rebuild from scratch
make clean && make all
```

### Expected Output

```
g++ -Wall -Wextra -std=c++17 -pthread -Iinclude -c src/controller.cpp -o obj/controller.o
g++ -Wall -Wextra -std=c++17 -pthread -Iinclude -c src/main.cpp -o obj/main.o
...
g++  obj/controller.o obj/main.o ... -o bin/traffic_sim -pthread
g++ ... -o bin/traffic_visualizer -lsfml-graphics -lsfml-window -lsfml-system
```

After building, you'll have:
- `bin/traffic_sim` - The main simulation
- `bin/traffic_visualizer` - The graphical visualizer

---

## ▶️ Running the Simulation

### Basic Usage

```bash
# Run the full simulation (Phase 3 with all features)
./bin/traffic_sim 3
```

### With Vehicle Limit

```bash
# Stop after 50 vehicles per intersection
./bin/traffic_sim 3 50
```

### Different Phases

```bash
# Phase 2: Single intersection only
./bin/traffic_sim 2

# Phase 3: Dual intersections + parking + emergency (RECOMMENDED)
./bin/traffic_sim 3
```

### Stopping the Simulation

Press `Ctrl+C` to gracefully stop the simulation.

---

## 🖼️ Running the SFML Visualizer

The visualizer shows the simulation graphically in real-time!

### Method 1: Two Terminals (Recommended)

**Terminal 1** - Start the simulation:
```bash
cd ~/Traffic-Simulation-OS
./bin/traffic_sim 3
```

**Terminal 2** - Start the visualizer:
```bash
cd ~/Traffic-Simulation-OS
./bin/traffic_visualizer
```

### Method 2: Background Process

```bash
# Start simulation in background, then visualizer
./bin/traffic_sim 3 &
sleep 2
./bin/traffic_visualizer
```

### Method 3: For WSL Users

If you're using WSL, make sure DISPLAY is set:

```bash
# Run simulation in background
./bin/traffic_sim 3 > logs/simulation.log 2>&1 &

# Wait a moment for logs to generate
sleep 3

# Run visualizer with display
DISPLAY=:0 ./bin/traffic_visualizer
```

### What You'll See

The visualizer window shows:

```
┌─────────────────────────────────────────────────────────────────┐
│  VEHICLE TYPES        │                           │  STATUS     │
│  A - Ambulance [P1]   │     <- IPC (Pipe) ->      │  Active: 5  │
│  F - Firetruck [P1]   │                           │  F10: 3     │
│  B - Bus [P2]         │    ┌────┐     ┌────┐      │  F11: 2     │
│  C - Car [P3]         │ ───│F10 │─────│F11 │───   │  Emergency:0│
│  K - Bike [P3]        │    └────┘     └────┘      │             │
│  T - Tractor [P3]     │       │           │       │  PARKING:   │
│                       │       │           │       │  Parked: 3/10│
│  PRIORITY LEVELS:     │  ┌────────┐               │  Queue: 1/5 │
│  [P1] Emergency       │  │PARKING │               │             │
│  [P2] Bus             │  │ LOT    │               │             │
│  [P3] Normal          │  └────────┘               │             │
│                       │                           │             │
│                       │                          │STATE INDICATORS:│
│                       │                           │Red = Waiting│
│     ┌────────┐        │                           │Green=Crossing│
│     │PARKING │        │                           │Blue = Parking│
│     │ LOT    |        │                           |Magenta=Transit|                  
││    └────────┘        |                           │              |
││    └────────┘        |                           │              |
└─────────────────────────────────────────────────────────────────┘
```

### Visualizer Controls
- **ESC** or close window button to exit
- Window auto-updates in real-time (60 FPS)

---

## ✅ Implementation Status

### Phase 0: Project Setup ✅ COMPLETE
- [x] Git repository initialized
- [x] Makefile with proper compilation flags
- [x] Directory structure (src/, include/, bin/, logs/, obj/)
- [x] README documentation

### Phase 1: Basic Simulation Skeleton ✅ COMPLETE
- [x] Main process as simulation entry point
- [x] Controller process created via `fork()`
- [x] Vehicle Generator thread spawning vehicle threads
- [x] Thread-safe logging system with timestamps
- [x] Signal handling (`SIGINT`) for graceful shutdown

### Phase 2: Safe Intersection Crossing ✅ COMPLETE
- [x] 4-quadrant intersection model (NW, NE, SW, SE)
- [x] Individual mutexes for each quadrant
- [x] Deadlock prevention via sorted lock acquisition
- [x] Concurrent crossing for non-conflicting paths
- [x] Vehicle-type based crossing times

### Phase 3: Dual Intersections with IPC ✅ COMPLETE
- [x] Two controller processes (F10 and F11)
- [x] Bidirectional pipes for communication
- [x] IPC message protocol (TRANSIT, EMERGENCY_CLEAR, SHUTDOWN, ACK)
- [x] Vehicle transit preserving ID and metadata
- [x] Dedicated IPC listener threads
 - [x] Concurrency safety: controllers wait for in-flight vehicle threads to finish before cleaning up intersection mutexes (prevents races when threads are still detached)

### Phase 4: Parking Lot Management ✅ COMPLETE
- [x] 10 parking spots controlled by semaphore
- [x] Bounded waiting queue (5 slots) with semaphore
- [x] Two-level admission control
- [x] Parking before crossing (no traffic blocking)
- [x] Parking duration simulation (3-7 seconds)

### Phase 5: Emergency Vehicle Preemption ✅ COMPLETE
- [x] Emergency vehicles (Ambulance, Firetruck) identified
- [x] Emergency mode activation at intersection
- [x] Normal vehicles blocked via condition variable
- [x] EMERGENCY_CLEAR signal via IPC to next intersection
- [x] EMERGENCY_PASSED signal to clear mode
- [x] Emergency vehicles bypass parking entirely

### Phase 6: SFML Visualization ✅ COMPLETE
- [x] Standalone visualizer binary
- [x] Real-time log file monitoring
- [x] Animated vehicle movement on roads
- [x] Two intersections with connecting road
- [x] IPC pipe indicator between intersections
- [x] All 6 vehicle types color-coded
- [x] Priority indicators [P1], [P2], [P3]
- [x] State indicators (Waiting/Crossing/Parking/Transit)
- [x] Emergency mode red flash overlay
- [x] Parking lot with 10 spots visualization
- [x] Waiting queue (5 slots) visualization
- [x] Status panel with live statistics
- [x] Legend panel explaining all symbols
- [x] Log panel showing recent events
- [x] Uses SFML (graphics/audio) for rendering and playback.
- [x] Visualizer is a separate binary (`bin/traffic_visualizer`).
- [x] Uses small amounts of STL (`std::vector`, `std::map`) in the visualization layer to manage textures, sprites, and UI state (core simulator remains POSIX/C-style).
- [x] PNG sprite support: loads high-quality vehicle PNG sprites when available with a threshold alpha mask to remove near-white halos.
- [x] Background audio: attempts to play background music (MP3/OGG) when available; playback is optional and skipped if files/codecs are missing.
- [x] Shows vehicles only from the most recent simulation run by locating the last run marker (`=== Traffic Simulation Started ===`) in `logs/traffic_sim.log` and tailing from that point.
- [x] UI improvements: parking panel moved to bottom-left, state indicators moved to bottom-right, legend icons added; the verbose event log is parsed but not shown as a persistent overlay by default.

---

## 📊 Requirements Compliance

### Intersection Requirements

| Requirement | Status | Implementation |
|-------------|--------|----------------|
| Two intersections (F10, F11) | ✅ | Two controller processes created via `fork()` |
| 4-way intersection model | ✅ | North, South, East, West entry/exit |
| Quadrant-based locking | ✅ | NW, NE, SW, SE mutexes per intersection |
| Deadlock prevention | ✅ | Sorted lock acquisition order |
| Concurrent non-conflicting paths | ✅ | N↔S and E↔W can cross simultaneously |

### Vehicle Requirements

| Requirement | Status | Implementation |
|-------------|--------|----------------|
| 6 vehicle types | ✅ | Ambulance, Firetruck, Bus, Car, Bike, Tractor |
| Unique vehicle IDs | ✅ | Sequential ID assignment per controller |
| Vehicle metadata | ✅ | Type, ID, origin, destination, priority |
| Random vehicle generation | ✅ | 1-3 second intervals with random types |
| Variable crossing times | ✅ | Ambulance: 1s, Car: 1.5s, Tractor: 3s |

### Priority Requirements

| Requirement | Status | Implementation |
|-------------|--------|----------------|
| Emergency priority (P1) | ✅ | Ambulance, Firetruck - preempts all |
| Bus priority (P2) | ✅ | Higher than normal vehicles |
| Normal priority (P3) | ✅ | Car, Bike, Tractor |
| Emergency preemption | ✅ | Condition variable blocks normal vehicles |

### IPC Requirements

| Requirement | Status | Implementation |
|-------------|--------|----------------|
| Pipes for F10↔F11 | ✅ | Bidirectional pipes via `pipe()` |
| Vehicle transit messages | ✅ | TRANSIT message with vehicle data |
| Emergency coordination | ✅ | EMERGENCY_CLEAR, EMERGENCY_PASSED signals |
| Acknowledgment protocol | ✅ | ACK messages for reliability |

### Parking Requirements

| Requirement | Status | Implementation |
|-------------|--------|----------------|
| 10 parking spots | ✅ | Semaphore-controlled resource |
| Bounded waiting queue | ✅ | 5-slot queue via semaphore |
| Semaphore-based access | ✅ | `sem_wait()` / `sem_post()` |
| No intersection blocking | ✅ | Acquire parking BEFORE crossing |
| Emergency bypass | ✅ | Emergency vehicles skip parking |

### Visualization Requirements

| Requirement | Status | Implementation |
|-------------|--------|----------------|
| Graphical display | ✅ | SFML 2.6 library |
| Real-time updates | ✅ | 60 FPS rendering, log polling |
| Vehicle animation | ✅ | Smooth movement along roads |
| Intersection visualization | ✅ | Gray boxes with labels |
| Emergency indication | ✅ | Red flash overlay + "EMERGENCY" text |
| Parking visualization | ✅ | Grid of 10 spots + queue |
| Status information | ✅ | Panels showing counts and states |

### What's NOT Implemented (Out of Scope)

| Feature | Reason |
|---------|--------|
| Traffic lights | Not in project requirements |
| Turn signals | Not in project requirements |
| Speed variations | Fixed crossing times per vehicle type |
| Weather effects | Not in project requirements |
| 3D graphics | 2D sufficient for demonstration |

---

## 📁 Project Architecture

### Directory Structure

```
Traffic-Simulation-OS/
│
├── 📄 Makefile                 # Build configuration
├── 📄 README.md                # This file
├── 📄 OS Simulation Project Plan.md
├── 📄 .gitignore
│
├── 📂 include/                 # Header files
│   ├── common.hpp              # Shared constants and structures
│   ├── controller.hpp          # Controller process declarations
│   ├── display.hpp             # SFML visualizer declarations
│   ├── parking.hpp             # Parking lot declarations
│   ├── utils.hpp               # Logging utilities
│   └── vehicle.hpp             # Vehicle thread declarations
│
├── 📂 src/                     # Source files
│   ├── main.cpp                # Entry point, process creation
│   ├── controller.cpp          # Intersection controller logic
│   ├── vehicle.cpp             # Vehicle thread logic
│   ├── parking.cpp             # Parking semaphore management
│   ├── utils.cpp               # Logging implementation
│   ├── display.cpp             # SFML rendering (~1200 lines)
│   └── visualizer_main.cpp     # Visualizer entry point
│
├── 📂 bin/                     # Compiled binaries (created by make)
│   ├── traffic_sim             # Main simulation
│   └── traffic_visualizer      # SFML visualizer
│
├── 📂 obj/                     # Object files (created by make)
│   ├── controller.o
│   ├── main.o
│   ├── parking.o
│   ├── utils.o
│   ├── vehicle.o
│   ├── display.o
│   └── visualizer_main.o
│
├── 📂 resources/               # Assets used by the visualizer
│   └── (icons, sprites, music)
│
└── 📂 logs/                    # Log files (created at runtime)
    └── traffic_sim.log         # Simulation event log
```

### Process Architecture

```
                    ┌─────────────────┐
                    │   Main Process  │
                    │   (main.cpp)    │
                    └────────┬────────┘
                             │ fork()
              ┌──────────────┴──────────────┐
              │                             │
              ▼                             ▼
    ┌─────────────────┐           ┌─────────────────┐
    │ F10 Controller  │◄─ PIPE ──►│ F11 Controller  │
    │   (Process)     │           │   (Process)     │
    └────────┬────────┘           └────────┬────────┘
             │                             │
    ┌────────┴────────┐           ┌────────┴────────┐
    │                 │           │                 │
    ▼                 ▼           ▼                 ▼
┌───────┐      ┌──────────┐  ┌───────┐      ┌──────────┐
│Vehicle│      │   IPC    │  │Vehicle│      │   IPC    │
│  Gen  │      │ Listener │  │  Gen  │      │ Listener │
│Thread │      │  Thread  │  │Thread │      │  Thread  │
└───┬───┘      └──────────┘  └───┬───┘      └──────────┘
    │                            │
    ▼                            ▼
┌─────────────────┐      ┌─────────────────┐
│ Vehicle Threads │      │ Vehicle Threads │
│  (per vehicle)  │      │  (per vehicle)  │
└─────────────────┘      └─────────────────┘
```

---

## 🔧 Troubleshooting

### Build Errors

**Error: `g++: command not found`**
```bash
sudo apt install build-essential
```

**Error: `SFML/Graphics.hpp: No such file`**
```bash
sudo apt install libsfml-dev
```

**Error: `undefined reference to sf::...`**
```bash
# Make sure you're building with make vis or make all
make clean && make all
```

### Runtime Errors

**Simulation exits immediately**
```bash
# Make sure logs directory exists
mkdir -p logs

# Check for error messages
./bin/traffic_sim 3 2>&1
```

**Visualizer window doesn't appear (WSL)**
```bash
# Check WSLg is installed
wsl --version

# Update WSL if needed
wsl --update

# Try setting DISPLAY explicitly
DISPLAY=:0 ./bin/traffic_visualizer
```

**Visualizer shows blank/no vehicles**
```bash
# Make sure simulation is running first
# Check that logs/traffic_sim.log is being written
tail -f logs/traffic_sim.log
```

### WSL-Specific Issues

**"Cannot open display"**
```bash
# Restart WSL
wsl --shutdown
# Then reopen Ubuntu terminal
```

**Graphics are slow**
```bash
# This is normal for WSLg
# The simulation itself runs at full speed
```

---

## 📝 Sample Output

### Console Output (Simulation)
```
=== Traffic Simulation Started ===
[2024-12-02 10:30:15] F10 Controller started (PID: 12345)
[2024-12-02 10:30:15] F11 Controller started (PID: 12346)
[2024-12-02 10:30:16] Vehicle 1 (Car) Arrived at F10 intersection North
[2024-12-02 10:30:16] Vehicle 1: ENTERING intersection F10
[2024-12-02 10:30:17] Vehicle 2 (Ambulance) Arrived at F10 intersection East
[2024-12-02 10:30:17] EMERGENCY MODE ACTIVATED at F10
[2024-12-02 10:30:17] Vehicle 2: ENTERING intersection F10 (EMERGENCY)
[2024-12-02 10:30:18] Vehicle 2: EXITING intersection F10 towards West
[2024-12-02 10:30:18] EMERGENCY MODE DEACTIVATED at F10
[2024-12-02 10:30:18] Vehicle 1: EXITING intersection F10 towards South
[2024-12-02 10:30:19] Vehicle 1: PARKING
[2024-12-02 10:30:19] Total parked: 1/10
...
```

---

## 👥 Authors

- **Shahoud867** - [GitHub Profile](https://github.com/Shahoud867)

---

## 📜 License

This project is created for educational purposes as part of an Operating Systems course.

---

## 🙏 Acknowledgments

- SFML Library for graphics
- POSIX threading and IPC APIs
- Course instructors and TAs
