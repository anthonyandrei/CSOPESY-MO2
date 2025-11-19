# CSOPESY MO2 Task Division (Multitasking OS & Memory Management)
---

## Member 1: Memory Manager & Page Handling
**Role:** Core Demand Paging & Backing Store Subsystem

### Responsibilities
- Physical memory abstraction (frames) and frame table (free/used tracking)  
- Per-process page tables (mapping virtual pages → physical frames/backing store offsets)  
- Allocation logic for:
  - `screen -s <process_name> <process_memory_size>`  
  - `screen -c <process_name> <process_memory_size> "<instructions>"`  
- Page fault detection and handler  
- Page replacement algorithm (FIFO or LRU)  
- Backing store file I/O (`csopesy-backing-store.txt`):
  - Page-out (evict frame → backing store)  
  - Page-in (load page → free frame)  
- Track paging metrics: pages in/out, faults, replacements  
- Enforce memory safety: invalid access triggers process termination & log  
- Provide query APIs for `process-smi` and `vmstat`

### Deliverables
- `memory_manager.*` (API + implementation)  
- Working page fault + replacement flow under load  
- Backing store lifecycle documented  
- Metrics accessible for reporting  

---

## Member 2: Process Representation & Instruction Engine (Lance)
**Role:** PCB + Instruction Execution + Memory Ops Bridge

### Responsibilities
- Extend PCB structure:
  - PID, Name, Memory Size, Page Table, Symbol Table (64 bytes)  
  - States: READY, RUNNING, SLEEPING, FINISHED, MEMORY-VIOLATED  
- Extended instruction parsing & execution:
  - Existing: PRINT, DECLARE, ADD, SUBTRACT, FOR, SLEEP  
  - New: READ var, addr & WRITE addr, value  
  - Clamp variable values to uint16  
- Integrate with Memory Manager:
  - Pre-check page presence; trigger page faults if absent  
  - WRITE: mark page dirty if tracking dirty bit  
- User-defined instruction parser for `screen -c`:
  - Validate syntax & instruction count (1–50)  
  - Reject if memory footprint exceeds declared memsize  
- Maintain per-process execution log (instructions, faults)

### Deliverables
- `process.*` (PCB + instruction engine)  
- Parser for custom process scripts  
- Execution logic that defers on page faults  
- Updated symbol table management & size enforcement  

---

## Member 3: Scheduler & CPU Tick System (Andrei)
**Role:** Instruction Dispatch, Timing, and Paging-Aware Scheduling

### Responsibilities
- Implement FCFS and Round Robin (RR) scheduling  
- Only dispatch instructions if required page is resident (else trigger fault path)  
- Stall/retry semantics on page fault  
- Handle SLEEP instructions & wake-ups  
- Manage queues: READY, SLEEPING, FINISHED, MEMORY-VIOLATED  
- Global CPU tick counter & per-tick execution loop  
- Track metrics:
  - Total ticks, idle ticks, active ticks  
  - Pages in/out (query Memory Manager)  
- Batch process generation (`scheduler-start`)  
- Respect `delays-per-exec`  

### Deliverables
- `scheduler.*` with tick loop & queue logic  
- Correct preemption (RR) & sequential dispatch (FCFS)  
- Accurate tick counters & idle vs active time  
- Verified stress scenario with multiple generated processes  

---

## Member 4: CLI, Commands, Output, Testing & Deliverables
**Role:** User Interface Layer & Reporting / Documentation

### Responsibilities
- Extend CLI parser for MO2 commands:
  - `screen -s <name> <memsize>`  
  - `screen -c <name> <memsize> "<instructions>"`  
  - `screen -ls`, `screen -r <name>`  
  - `scheduler-start`, `scheduler-stop`  
  - `report-util`, `process-smi`, `vmstat`  
- Screen sub-commands: `process-smi`, `exit`  
- Format outputs:
  - Aligned tables, headers refresh  
  - NVIDIA-style `process-smi` output (PID, Name, State, RSS, Faults, Pages In/Out)  
  - `vmstat` columns: ticks, idle%, pages-in/out, active process count  
- Handle error & status messages:
  - Invalid command, memory allocation failure  
  - Access violation termination notice  
  - Process completion message  
- Testing scripts & scenario preparation  
- Deliverables preparation:
  - README.txt, sample `config.txt`  
  - PowerPoint technical report  
  - Final video demonstration  

### Deliverables
- `cli.*` implementing parsing & dispatch  
- Consistent, testable formatted console outputs  
- Validation feedback for all command errors  
- Complete documentation and presentation materials
