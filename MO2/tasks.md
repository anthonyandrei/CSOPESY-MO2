# CSOPESY MO2 Task Division (Multitasking OS & Memory Management)

---

Assumptions: MO1 is complete (CLI shell + `initialize` + config parsing, FCFS/RR scheduler core, screen commands, report-util baseline, process model and basic instructions). MO2 tasks below are deltas that reuse MO1 modules and avoid overlap.

---

## Member 1: Memory Manager & Page Handling (Leomarc)

**Role:** Core Demand Paging & Backing Store Subsystem

### Responsibilities

-   Pre-allocate main memory on `initialize` using config:
    -   `max-overall-mem` (bytes), `mem-per-frame` (bytes/page)
    -   Compute total frames = `max-overall-mem / mem-per-frame`
-   Physical memory abstraction (frames) and frame table (free/used tracking)
-   Per-process page tables (virtual page → physical frame or backing-store offset)
-   Allocation logic for `screen -s` / `screen -c` based on per-process mem size
-   Page fault detection and handler (not-present vs invalid/protection)
-   Page replacement algorithm (FIFO or LRU, from config `replacement-policy`)
-   Backing store file I/O (`csopesy-backing-store.txt`):
    -   Page-out (evict frame → backing store)
    -   Page-in (load page → free frame)
-   Track metrics: pages in/out, faults, replacements; expose per-process RSS
-   Enforce memory safety: invalid access → terminate process and log timestamp + offending hex address
-   Provide query APIs for `process-smi` and `vmstat` (used/free/total memory, paging counters)

### Deliverables

-   [X] `memory_manager.*` (API + implementation)
-   [X] Working page fault + replacement flow under load
-   [X] Backing store lifecycle documented
-   [X] Metrics accessible for reporting

---

## Member 2: Process Representation & Instruction Engine (Lance)
**Role:** PCB + Instruction Execution + Memory Ops Bridge

### Responsibilities

-   Extend existing PCB structure (reuse MO1 fields), add:
    -   PID, Name, Memory Size, Page Table, Symbol Table (64 bytes)
    -   States: READY, RUNNING, SLEEPING, FINISHED, MEMORY-VIOLATED
-   Extended instruction parsing & execution:
    -   Existing: PRINT, DECLARE, ADD, SUBTRACT, FOR, SLEEP
    -   New: READ var, addr & WRITE addr, value
    -   Clamp variable values to uint16 (0–65535)
-   Integrate with Memory Manager:
    -   Parse hexadecimal addresses for READ/WRITE (e.g., `0x500`); reject non-hex
    -   Validate address range within process memory size; out-of-range → violation
    -   Pre-check page presence; if not resident, raise page fault status (defer exec)
    -   On WRITE, mark page dirty if a dirty bit is tracked
-   User-defined instruction parser for `screen -c`:
    -   Validate syntax & instruction count (1–50)
    -   Reject if memory footprint exceeds declared memsize
-   Maintain per-process execution log (instructions, faults)
-   Ensure DECLARE/variable ops require the symbol-table page to be resident; otherwise fault and retry after page-in

### Deliverables

-   [X] `process.*` (PCB + instruction engine)
-   [X] Parser for custom process scripts
-   [X] Execution logic that defers on page faults
-   [X] Updated symbol table management & size enforcement

---

## Member 3: Scheduler & CPU Tick System (Andrei)

**Role:** Instruction Dispatch, Timing, and Paging-Aware Scheduling

### Responsibilities

-   Reuse MO1 FCFS/RR scheduler core; read `scheduler` and `quantum-cycles` from config
-   Only dispatch instructions if required page is resident (else trigger fault path)
-   Stall/retry semantics on page fault
-   Handle SLEEP instructions & wake-ups
-   Manage queues: READY, SLEEPING, FINISHED, MEMORY-VIOLATED
-   Global CPU tick counter & per-tick execution loop
-   Track metrics:
    -   Total ticks, idle ticks, active ticks
    -   Pages in/out (query Memory Manager)
-   Batch process generation (`scheduler-start`) including READ/WRITE in generated scripts respecting `min-ins`/`max-ins`
-   Respect `delays-per-exec` as busy-wait (process holds CPU during delay)
### Deliverables

-   [X] `scheduler.*` with tick loop & queue logic
-   [X] Correct preemption (RR) & sequential dispatch (FCFS)
-   [X] Accurate tick counters & idle vs active time
-   [X] Verified stress scenario with multiple generated processes

---

## Member 4: CLI, Commands, Output, Testing & Deliverables (Brent)

**Role:** User Interface Layer & Reporting / Documentation

### Responsibilities

- [ ] Extend existing MO1 CLI parser (no re-implementation) with MO2 specifics:
    - [ ] `screen -s <name> <memsize>` (memsize must be power-of-two within [64, 65536] bytes)
    - [ ] `screen -c <name> <memsize> "<instructions>"` (1–50 semicolon-separated commands)
    - [ ] `screen -r <name>` (update `screen -r` to show violation message below)
    - [ ] `process-smi`
    - [ ] `vmstat`
- [ ] Format outputs:
    - [ ] Aligned tables, headers refresh
    - [ ] `process-smi` header with total/used/free memory; per-process rows: PID | Name | State | RSS | Faults | PagesIn | PagesOut
    - [ ] `vmstat` table columns: total_mem | used_mem | free_mem | ticks_total | ticks_active | ticks_idle | pages_in | pages_out | procs_active
- [ ] Handle error & status messages:
    - [ ] Invalid command
    - [ ] memory allocation failure
    - [ ] Access violation termination notice with exact spec format:
        - [ ] `Process <name> shut down due to memory access violation error that occurred at <HH:MM:SS>. <Hex memory address> invalid.`
    - [ ] Process completion message
- [ ] Deliverables preparation:
    - [X] README.txt
    - [ ] PowerPoint technical report
    - [ ] Submission

### Deliverables

-   `cli.*` implementing parsing & dispatch
-   Consistent, testable formatted console outputs
-   Validation feedback for all command errors
-   Complete documentation and presentation materials
---
