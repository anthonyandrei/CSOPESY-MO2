## Member 1: Andrei
**Role:** Command-line Parser & Configuration Handler  

### Responsibilities
- Implement the **main CLI shell** and **command interpreter**
- Parse and execute user commands:
  - `initialize`
  - `exit`
  - `screen`
  - `scheduler-start`
  - `scheduler-stop`
  - `report-util`
- Handle **input tokenization** and **command validation**
- Implement the **`initialize`** command:
  - Read parameters from `config.txt`
  - Validate values and enforce constraints
  - Prevent other commands until initialization is completed

### Deliverables
- Functional CLI shell
- Correct parsing of `config.txt`
- Error handling for invalid commands or parameters
- Command recognition confirmed in testing

---

## Member 2: Lance Desmond Labarrete
**Role:** Process Management & Virtual Screen Interface  

### Responsibilities
- Design and implement the **Process structure**:
  - Process ID, name (e.g., `p01`, `p02`, ...)
  - States: READY, RUNNING, SLEEPING, FINISHED
  - Instruction list and variable memory
- Implement **process instructions**:
  - `PRINT`, `DECLARE`, `ADD`, `SUBTRACT`, `SLEEP`, `FOR`
- Implement **`screen` command system**:
  - `screen -s <process>` – create and attach new process
  - `screen -r <process>` – reattach existing process
  - `screen -ls` – list all processes
  - Inside process screen:
    - `process-smi` – show process info/logs
    - `exit` – return to main menu

### Deliverables
- Working Process simulation with instruction execution
- Fully functional screen attachment system
- Accurate variable handling and process logging

---

## Member 3: Leomarc
**Role:** CPU Tick System & Scheduling Logic  

### Responsibilities
- Implement **CPU tick simulation** (global counter)
- Implement **scheduling algorithms**:
  - First-Come, First-Served (FCFS)
  - Round Robin (RR)
- Manage **ready**, **sleeping**, and **finished** queues
- Implement **process dispatching and preemption**
- Handle:
  - `scheduler-start` – generate processes periodically
  - `scheduler-stop` – halt generation
- Assign processes to CPUs based on availability

### Deliverables
- Functional CPU scheduler loop
- Accurate process switching and timing
- Support for both FCFS and RR modes
- Dynamic process generation with `batch-process-freq`

---

## Member 4: 
**Role:** Reports, Testing, and Presentation  

### Responsibilities
- Implement **`report-util`**:
  - Compute and display CPU utilization
  - Summarize cores used, processes running/finished
  - Save report to `csopesy-log.txt`
- Create **testing scripts and scenarios**
- Perform full feature testing for all commands
- Prepare documentation:
  - `README.txt` (run instructions, authors)
  - Sample `config.txt`
  - PowerPoint technical report
  - Final video demonstration

### Deliverables
- Correct and formatted utilization report
- Verified test results for all modules
- Complete documentation and presentation materials
