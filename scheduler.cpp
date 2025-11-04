/**
 * @file scheduler.cpp
 * @brief CPU scheduler and process execution implementation
 */

#include "scheduler.h"
#include "config.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>

// External references from main.cpp
extern Config config;
extern bool isInitialized;
extern bool verboseMode;

// ============================================================================
// Global scheduler state
// ============================================================================
constexpr int UINT16_MAX_VALUE = 65535;                ///< Maximum value for uint16 variables
constexpr int UINT16_MIN_VALUE = 0;                    ///< Minimum value for uint16 variables
constexpr int MAX_FOR_LOOP_DEPTH = 3;                  ///< Maximum FOR loop nesting depth per specs pg. 3

std::atomic<uint64_t> global_cpu_tick(0);              ///< Global CPU tick counter
std::atomic<bool> is_generating_processes(false);      ///< True when scheduler-start is active
std::atomic<int> next_process_id(1);                   ///< Next process ID to assign

std::mutex queue_mutex;                                ///< Protects all queues and cpu_cores

std::list<Process> ready_queue;                        ///< Processes waiting for CPU
std::list<Process> sleeping_queue;                     ///< Processes blocked on SLEEP
std::list<Process> finished_queue;                     ///< Completed processes

std::vector<std::optional<Process>> cpu_cores;         ///< Per-core running process

// ============================================================================
// Process generation
// ============================================================================

/**
 * @brief Generate a new process with random instruction count
 * 
 * Creates a process with [minIns, maxIns] instructions (random).
 * Process name follows pattern: p01, p02, ..., p1240
 * New process is added to ready_queue.
 * 
 * Called by scheduler_loop() every batchProcessFreq ticks when
 * is_generating_processes is true.
 */
void generate_new_process() {
    // Get instruction count range from config
    uint32_t min_ins = config.minIns;
    uint32_t max_ins = config.maxIns;

    // Generate random instruction count in [min_ins, max_ins]
    uint32_t num_instructions = min_ins;
    if (max_ins > min_ins) {
        num_instructions = (rand() % (max_ins - min_ins + 1)) + min_ins;
    }

    // Generate process name (p01, p02, ...)
    int pid = next_process_id++;
    std::string pname = std::string("p") + (pid < 10 ? "0" : "") + std::to_string(pid);

    if (verboseMode) {
        std::cout << "\n[Scheduler] Generating process " << pname
            << " (" << num_instructions << " instructions)." << std::endl;
    }

    // Add to ready queue (thread-safe)
    std::lock_guard<std::mutex> lock(queue_mutex);
    ready_queue.emplace_back(pid, pname, num_instructions);
}

// ============================================================================
// Queue management
// ============================================================================

/**
 * @brief Wake up sleeping processes whose timers have expired
 * 
 * Scans sleeping_queue and moves processes with sleep_until_tick <= current_tick
 * back to ready_queue. Called once per CPU tick.
 */
void check_sleeping() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    uint64_t current_tick = global_cpu_tick.load();

    auto it = sleeping_queue.begin();
    while (it != sleeping_queue.end()) {
        if (current_tick >= it->sleep_until_tick) {
            if (verboseMode) 
                std::cout << "\n[Scheduler] Process " << it->name << " is WAKING UP." << std::endl;
            
            it->state = ProcessState::READY;
            ready_queue.push_back(std::move(*it));
            it = sleeping_queue.erase(it);
        }
        else {
            ++it;
        }
    }
}

/**
 * @brief Dispatch ready processes to idle CPU cores
 * 
 * Assigns processes from ready_queue to cpu_cores with no active process.
 * For FCFS: process runs until completion or sleep.
 * For RR: process quantum is set to quantumCycles.
 * 
 * Pops from front of ready_queue (FIFO order).
 */
void dispatch_processes() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    if (ready_queue.empty()) {
        return;
    }

    for (int i = 0; i < cpu_cores.size(); ++i) {
        if (!cpu_cores[i].has_value()) {
            // Pop process from ready queue (FCFS/FIFO)
            Process p = std::move(ready_queue.front());
            ready_queue.pop_front();

            p.state = ProcessState::RUNNING;

            // Set quantum for Round Robin
            if (config.scheduler == "rr") {
                p.quantum_ticks_left = config.quantumCycles;
            }

            if (verboseMode) 
                std::cout << "\n[Scheduler] DISPATCHING " << p.name 
                          << " to CPU " << i << "." << std::endl;
            
            cpu_cores[i] = std::move(p);

            // Stop if no more ready processes
            if (ready_queue.empty()) {
                break;
            }
        }
    }
}
// ============================================================================
// CPU execution
// ============================================================================

/**
 * @brief Execute one CPU tick for all running processes
 * 
 * For each occupied CPU core:
 * 1. Execute one instruction via execute_instruction()
 * 2. Check if process finished or sleeping (if so, remove from core)
 * 3. For RR: decrement quantum, preempt if quantum expires
 * 
 * Called once per scheduler loop iteration (every 100ms real time).
 */
void execute_cpu_tick() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    uint64_t current_tick = global_cpu_tick.load();

    for (int i = 0; i < cpu_cores.size(); ++i) {
        if (cpu_cores[i].has_value()) {
            Process& p = *cpu_cores[i];

            // Execute one instruction
            execute_instruction(p, current_tick);

            // Remove from core if finished or sleeping
            if (p.state == ProcessState::FINISHED || p.state == ProcessState::SLEEPING) {
                cpu_cores[i].reset();
                continue;
            }

            // Round Robin quantum management
            if (config.scheduler == "rr") {
                p.quantum_ticks_left--;
                if (p.quantum_ticks_left == 0) {
                    if (verboseMode) 
                        std::cout << "\n[Scheduler] Process " << p.name 
                                  << " PREEMPTED (RR)." << std::endl;
                    
                    p.state = ProcessState::READY;
                    ready_queue.push_back(std::move(p));
                    cpu_cores[i].reset();
                }
            }
        }
    }
}

// ============================================================================
// Instruction execution
// ============================================================================

/**
 * @brief Execute one instruction of a process
 * @param p Process to execute (modified in-place)
 * @param current_tick Current global CPU tick
 * 
 * Executes p.instructions[p.current_instruction] and updates state.
 * Supported instructions (specs pg. 2-3):
 * - PRINT <message>: Output message to console
 * - DECLARE <var> <value>: Initialize variable
 * - ADD <var> <value>: Add to variable
 * - SUBTRACT <var> <value>: Subtract from variable
 * - SLEEP <ticks>: Block process for <ticks> CPU ticks
 * - FOR <count>: Duplicate next instruction <count> times
 * 
 * Variables are stored in p.memory (uint16 clamped to [0, 65535]).
 * Undeclared variables default to 0.
 * 
 * On completion (all instructions executed), process moves to finished_queue.
 * On SLEEP, process moves to sleeping_queue.
 */
void execute_instruction(Process& p, uint64_t current_tick) {
    // Implement delays-per-exec: busy-wait before executing instruction
    if (p.delay_ticks_left > 0) {
        p.delay_ticks_left--;
        return;  // Process remains in CPU but doesn't execute yet
    }

    // Check if all instructions completed
    if (p.current_instruction >= p.instructions.size()) {
        if (verboseMode) 
            std::cout << "\n[Scheduler] Process " << p.name << " FINISHED." << std::endl;
        
        p.state = ProcessState::FINISHED;
        finished_queue.push_back(std::move(p));
        return;
    }

    Instruction& ins = p.instructions[p.current_instruction];

    // Execute instruction based on operation
    if (ins.op == "PRINT") {
        std::cout << "[" << p.name << "] " << ins.args[0] << std::endl;
    }
    else if (ins.op == "DECLARE") {
        // Initialize variable and clamp to uint16 range [0, 65535]
        int value = std::stoi(ins.args[1]);
        if (value < UINT16_MIN_VALUE) value = UINT16_MIN_VALUE;
        if (value > UINT16_MAX_VALUE) value = UINT16_MAX_VALUE;
        p.memory[ins.args[0]] = value;
    }
    else if (ins.op == "ADD") {
        // Add to variable and clamp to uint16 range [0, 65535]
        int result = p.memory[ins.args[0]] + std::stoi(ins.args[1]);
        if (result < UINT16_MIN_VALUE) result = UINT16_MIN_VALUE;
        if (result > UINT16_MAX_VALUE) result = UINT16_MAX_VALUE;
        p.memory[ins.args[0]] = result;
    }
    else if (ins.op == "SUBTRACT") {
        // Subtract from variable and clamp to uint16 range [0, 65535]
        int result = p.memory[ins.args[0]] - std::stoi(ins.args[1]);
        if (result < UINT16_MIN_VALUE) result = UINT16_MIN_VALUE;
        if (result > UINT16_MAX_VALUE) result = UINT16_MAX_VALUE;
        p.memory[ins.args[0]] = result;
    }
    else if (ins.op == "SLEEP") {
        // Block process for specified ticks
        p.state = ProcessState::SLEEPING;
        p.sleep_until_tick = current_tick + std::stoi(ins.args[0]);
        sleeping_queue.push_back(std::move(p));
        return;
    }
    else if (ins.op == "FOR") {
        // Duplicate next instruction <count> times with nesting depth validation
        int count = std::stoi(ins.args[0]);
        
        if (p.current_instruction + 1 < p.instructions.size()) {
            Instruction& nextIns = p.instructions[p.current_instruction + 1];
            
            // Check if next instruction is a nested FOR loop
            if (nextIns.op == "FOR") {
                // Validate nesting depth doesn't exceed max (3 levels per specs pg. 3)
                if (p.for_loop_depth >= MAX_FOR_LOOP_DEPTH) {
                    if (verboseMode)
                        std::cout << "[" << p.name << "] ERROR: FOR loop nesting exceeds max depth of "
                                  << MAX_FOR_LOOP_DEPTH << "\n";
                    p.current_instruction++;
                    return;
                }
                p.for_loop_depth++;
            }
            
            // Duplicate the next instruction (count - 1) times
            for (int i = 0; i < count - 1; i++) {
                p.instructions.insert(p.instructions.begin() + p.current_instruction + 1, nextIns);
            }
        }
    }

    // Move to next instruction
    p.current_instruction++;
    
    // Reset delay counter for next instruction (busy-wait per spec pg. 4)
    p.delay_ticks_left = config.delaysPerExec;
}
// ============================================================================
// Scheduler main loop
// ============================================================================

/**
 * @brief Main scheduler loop (runs in background thread)
 * 
 * Executes continuously after initialization:
 * 1. Increment global_cpu_tick
 * 2. Generate new process if is_generating_processes and time elapsed >= batchProcessFreq
 * 3. Wake up sleeping processes (check_sleeping)
 * 4. Execute one tick on all running processes (execute_cpu_tick)
 * 5. Dispatch ready processes to idle cores (dispatch_processes)
 * 6. Sleep 100ms (simulates CPU tick delay)
 * 
 * Only runs when isInitialized is true (guard exists for safety).
 */
void scheduler_loop() {
    uint64_t last_generation_tick = 0;

    while (true) {
        if (isInitialized) {
            // Increment global CPU tick
            global_cpu_tick++;
            uint64_t current_tick = global_cpu_tick.load();

            // Periodic process generation
            if (is_generating_processes.load() &&
                (current_tick - last_generation_tick >= config.batchProcessFreq))
            {
                last_generation_tick = current_tick;
                generate_new_process();
            }

            // Process lifecycle management
            check_sleeping();          // Wake up sleeping processes
            execute_cpu_tick();        // Execute instructions
            dispatch_processes();      // Assign ready processes to CPUs
        }

        // Simulate CPU tick delay (100ms real time = 1 CPU tick)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Start background scheduler thread
 * 
 * Spawns detached thread running scheduler_loop().
 * Called once after successful initialization.
 */
void start_scheduler_thread() {
    std::thread(scheduler_loop).detach();
}

/**
 * @brief Enable periodic process generation
 * 
 * Sets is_generating_processes flag. Processes are created
 * every batchProcessFreq ticks in scheduler_loop().
 */
void start_process_generation() {
    is_generating_processes = true;
}

/**
 * @brief Disable periodic process generation
 * 
 * Clears is_generating_processes flag. Already-created
 * processes continue executing.
 */
void stop_process_generation() {
    is_generating_processes = false;
}