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
constexpr int NUM_INSTRUCTION_TYPES = 5;               ///< Number of instruction types (PRINT, DECLARE, ADD, SUBTRACT, SLEEP)
constexpr int NUM_INSTRUCTION_TYPES_WITH_FOR = 6;     ///< Number of instruction types including FOR
constexpr int FOR_LOOP_PROBABILITY = 10;              ///< 1 in N chance to generate a FOR loop
constexpr int MIN_FOR_ITERATIONS = 2;                 ///< Minimum FOR loop iterations
constexpr int MAX_FOR_ITERATIONS = 5;                 ///< Maximum FOR loop iterations
constexpr int MIN_FOR_BODY_SIZE = 2;                  ///< Minimum instructions in FOR loop body
constexpr int MAX_FOR_BODY_SIZE = 5;                  ///< Maximum instructions in FOR loop body
constexpr int PROCESS_NAME_PADDING_THRESHOLD = 10;     ///< Process IDs below this get zero-padded (p01, p02, etc.)
constexpr int MAX_DECLARE_VALUE = 100;                 ///< Maximum random value for DECLARE instruction (0-99)
constexpr int MAX_ARITHMETIC_OPERAND = 50;             ///< Maximum random value for ADD/SUBTRACT operands (0-49)
constexpr int MIN_SLEEP_TICKS = 1;                     ///< Minimum sleep duration in CPU ticks
constexpr int MAX_SLEEP_TICKS = 10;                    ///< Maximum sleep duration in CPU ticks
constexpr int PROBABILITY_DENOMINATOR = 2;             ///< Denominator for 50% probability checks
constexpr int REQUIRED_OPERANDS_FOR_ARITHMETIC = 3;    ///< Required operand count for ADD/SUBTRACT
constexpr int CPU_TICK_DELAY_MS = 100;                 ///< Real-time delay per CPU tick in milliseconds

std::atomic<uint64_t> global_cpu_tick(0);              ///< Global CPU tick counter
std::atomic<bool> is_generating_processes(false);      ///< True when scheduler-start is active
std::atomic<int> next_process_id(1);                   ///< Next process ID to assign

std::mutex queue_mutex;                                ///< Protects all queues and cpu_cores

std::list<Process> ready_queue;                        ///< Processes waiting for CPU
std::list<Process> sleeping_queue;                     ///< Processes blocked on SLEEP
std::list<Process> finished_queue;                     ///< Completed processes

std::vector<std::optional<Process>> cpu_cores;         ///< Per-core running process

// ============================================================================
// Helper functions
// ============================================================================

/**
 * @brief Generate random integer in range [min, max] inclusive
 */
int random_in_range(int min, int max) {
    return min + (rand() % (max - min + 1));
}

/**
 * @brief Generate process name with zero-padding (e.g., p01, p02, ..., p10, p11)
 */
std::string generate_process_name(int pid) {
    std::string padding = (pid < PROCESS_NAME_PADDING_THRESHOLD) ? "0" : "";
    return "p" + padding + std::to_string(pid);
}

/**
 * @brief Process PRINT message with variable concatenation
 * @param message Template message with +varname patterns
 * @param p Process with memory context
 * @return Processed message with variables replaced by their values
 * 
 * Replaces patterns like "+x" with the value of variable x.
 * Auto-initializes undeclared variables to 0.
 */
std::string process_print_message(std::string message, Process& p) {
    size_t pos = 0;
    
    // Look for pattern: +varname (variable concatenation)
    while ((pos = message.find('+', pos)) != std::string::npos) {
        // Extract variable name after '+'
        size_t varStart = pos + 1;
        size_t varEnd = varStart;
        
        // Find the end of the variable name (alphanumeric + underscore)
        while (varEnd < message.length() && 
               (std::isalnum(message[varEnd]) || message[varEnd] == '_')) {
            varEnd++;
        }
        
        if (varEnd > varStart) {
            std::string varName = message.substr(varStart, varEnd - varStart);
            
            // Get variable value (auto-initialize to 0 if not declared)
            if (p.memory.find(varName) == p.memory.end()) {
                p.memory[varName] = 0;  // Auto-declare as per specs pg. 3
            }
            int varValue = p.memory[varName];
            
            // Replace +varName with the variable value
            message.replace(pos, varEnd - pos, std::to_string(varValue));
        }
        
        pos++;
    }
    
    return message;
}

/**
 * @brief Clamp integer value to uint16 range [0, 65535]
 */
int clamp_to_uint16(int value) {
    if (value < UINT16_MIN_VALUE) return UINT16_MIN_VALUE;
    if (value > UINT16_MAX_VALUE) return UINT16_MAX_VALUE;
    return value;
}

/**
 * @brief Get value of an operand (variable or literal)
 * @param operand String containing variable name or numeric literal
 * @param p Process with memory context
 * @return Resolved integer value
 */
int get_operand_value(const std::string& operand, Process& p) {
    // Check if it's a numeric literal
    if (std::isdigit(operand[0]) || (operand[0] == '-' && operand.length() > 1)) {
        return std::stoi(operand);
    }
    
    // It's a variable - auto-initialize to 0 if not declared
    if (p.memory.find(operand) == p.memory.end()) {
        p.memory[operand] = 0;
    }
    return p.memory[operand];
}

/**
 * @brief Generate random operand (50% variable, 50% literal)
 * @param var_pool Pool of variable names to choose from
 * @param max_literal Maximum value for literal operands
 * @return String containing either a variable name or numeric literal
 */
std::string generate_random_operand(const std::vector<std::string>& var_pool, int max_literal) {
    // 50% chance: return variable, 50% chance: return literal
    return (rand() % 2 == 0) 
        ? var_pool[rand() % var_pool.size()]
        : std::to_string(rand() % max_literal);
}

/**
 * @brief Execute arithmetic operation (ADD or SUBTRACT)
 * @param p Process to execute on
 * @param ins Instruction with operands
 * @param is_add True for addition, false for subtraction
 */
void execute_arithmetic(Process& p, const Instruction& ins, bool is_add) {
    if (ins.args.size() < REQUIRED_OPERANDS_FOR_ARITHMETIC) {
        if (verboseMode)
            std::cout << "[" << p.name << "] ERROR: " << ins.op 
                      << " requires 3 operands\n";
        return;
    }
    
    std::string var1 = ins.args[0];
    int value2 = get_operand_value(ins.args[1], p);
    int value3 = get_operand_value(ins.args[2], p);
    
    int result = is_add ? (value2 + value3) : (value2 - value3);
    p.memory[var1] = clamp_to_uint16(result);
}

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
 * Can generate FOR loops with format: FOR(repeats, block_size).
 * Respects MAX_FOR_LOOP_DEPTH nesting limit.
 * 
 * Called by scheduler_loop() every batchProcessFreq ticks when
 * is_generating_processes is true.
 */
void generate_new_process() {
    // Get instruction count range from config
    uint32_t min_ins = config.minIns;
    uint32_t max_ins = config.maxIns;

    // Generate random instruction count in [min_ins, max_ins]
    uint32_t num_instructions = (max_ins > min_ins) 
        ? random_in_range(min_ins, max_ins)
        : min_ins;

    // Generate process name (p01, p02, ...)
    int pid = next_process_id++;
    std::string pname = generate_process_name(pid);

    if (verboseMode) {
        std::cout << "\n[Scheduler] Generating process " << pname
            << " (" << num_instructions << " instructions)." << std::endl;
    }

    // Create the process
    Process p(pid, pname, num_instructions);
    
    // Variable pool for instruction generation
    std::vector<std::string> var_pool = {"x", "y", "z", "counter", "sum", "temp", "result", "value"};
    
    // Generate instructions in a flat loop with FOR(repeats, block_size) format
    for (uint32_t i = 0; i < num_instructions; i++) {
        Instruction ins;
        
        // 10% chance of FOR if we have room for loop body
        uint32_t remaining_instructions = num_instructions - i - 1;
        bool can_generate_for = remaining_instructions >= MIN_FOR_BODY_SIZE &&
                                rand() % FOR_LOOP_PROBABILITY == 0;
        
        if (can_generate_for) {
            // Generate FOR instruction with block_size
            ins.op = "FOR";
            
            // Random iteration count
            int iterations = random_in_range(MIN_FOR_ITERATIONS, MAX_FOR_ITERATIONS);
            ins.args.push_back(std::to_string(iterations));
            
            // Random block size (clamped to remaining instructions)
            int max_block = std::min(static_cast<int>(remaining_instructions), MAX_FOR_BODY_SIZE);
            int block_size = random_in_range(MIN_FOR_BODY_SIZE, max_block);
            ins.args.push_back(std::to_string(block_size));
        } else {
            // Generate regular instruction (PRINT, DECLARE, ADD, SUBTRACT, SLEEP)
            int instruction_type = rand() % NUM_INSTRUCTION_TYPES;
            
            switch (instruction_type) {
                case 0: // PRINT
                    ins.op = "PRINT";
                    // No args = use default message in execute_instruction
                    break;
                    
                case 1: // DECLARE
                    ins.op = "DECLARE";
                    ins.args.push_back(var_pool[rand() % var_pool.size()]);
                    ins.args.push_back(std::to_string(rand() % MAX_DECLARE_VALUE));
                    break;
                    
                case 2: // ADD
                    ins.op = "ADD";
                    ins.args.push_back(var_pool[rand() % var_pool.size()]);
                    ins.args.push_back(generate_random_operand(var_pool, MAX_ARITHMETIC_OPERAND));
                    ins.args.push_back(generate_random_operand(var_pool, MAX_ARITHMETIC_OPERAND));
                    break;
                    
                case 3: // SUBTRACT
                    ins.op = "SUBTRACT";
                    ins.args.push_back(var_pool[rand() % var_pool.size()]);
                    ins.args.push_back(generate_random_operand(var_pool, MAX_ARITHMETIC_OPERAND));
                    ins.args.push_back(generate_random_operand(var_pool, MAX_ARITHMETIC_OPERAND));
                    break;
                    
                case 4: // SLEEP
                    ins.op = "SLEEP";
                    ins.args.push_back(std::to_string(random_in_range(MIN_SLEEP_TICKS, MAX_SLEEP_TICKS)));
                    break;
            }
        }
        
        p.instructions.push_back(ins);
    }

    // Add to ready queue (thread-safe)
    std::lock_guard<std::mutex> lock(queue_mutex);
    ready_queue.push_back(std::move(p));
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
 * - PRINT <message>: Output message to console (supports variable concatenation: +varname)
 * - DECLARE <var> <value>: Initialize variable
 * - ADD <var1> <var2/value> <var3/value>: var1 = var2/value + var3/value
 * - SUBTRACT <var1> <var2/value> <var3/value>: var1 = var2/value - var3/value
 * - SLEEP <ticks>: Block process for <ticks> CPU ticks
 * - FOR <count>: Duplicate next instruction <count> times
 * 
 * Variables are stored in p.memory (uint16 clamped to [0, 65535]).
 * Undeclared variables auto-initialize to 0.
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
        // PRINT instruction can handle variable concatenation: PRINT ("Value from: " +x)
        // If no argument provided, use default message
        std::string message = ins.args.empty() 
            ? "Hello world from " + p.name + "!" 
            : ins.args[0];
        
        // Process variable concatenation (+varname patterns)
        message = process_print_message(message, p);
        std::cout << "[" << p.name << "] " << message << std::endl;
    }
    else if (ins.op == "DECLARE") {
        // Initialize variable and clamp to uint16 range [0, 65535]
        int value = std::stoi(ins.args[1]);
        p.memory[ins.args[0]] = clamp_to_uint16(value);
    }
    else if (ins.op == "ADD") {
        execute_arithmetic(p, ins, true);
    }
    else if (ins.op == "SUBTRACT") {
        execute_arithmetic(p, ins, false);
    }
    else if (ins.op == "SLEEP") {
        // Block process for specified ticks
        p.state = ProcessState::SLEEPING;
        p.sleep_until_tick = current_tick + std::stoi(ins.args[0]);
        sleeping_queue.push_back(std::move(p));
        return;
    }
    else if (ins.op == "FOR") {
        // FOR loop: Execute a block of instructions multiple times
        // Format: FOR <iterations> <block_size>
        
        if (ins.args.size() < 2) {
            if (verboseMode)
                std::cout << "[" << p.name << "] ERROR: FOR requires 2 arguments (iterations, block_size)\n";
            p.current_instruction++;
            return;
        }
        
        int iterations = std::stoi(ins.args[0]);
        int block_size = std::stoi(ins.args[1]);
        
        // Check nesting depth limit
        if (p.loop_stack.size() >= MAX_FOR_LOOP_DEPTH) {
            if (verboseMode)
                std::cout << "[" << p.name << "] ERROR: FOR loop nesting exceeds max depth of "
                          << MAX_FOR_LOOP_DEPTH << "\n";
            p.current_instruction++;
            return;
        }
        
        // Validate block size
        uint32_t loop_start = p.current_instruction + 1;
        uint32_t loop_end = p.current_instruction + block_size;
        
        if (loop_start >= p.instructions.size() || loop_end > p.instructions.size()) {
            if (verboseMode)
                std::cout << "[" << p.name << "] ERROR: FOR loop block_size exceeds instruction bounds\n";
            p.current_instruction++;
            return;
        }
        
        // Push loop frame onto stack
        LoopStruct frame;
        frame.loop_start = loop_start;
        frame.loop_end = loop_end;
        frame.iterations_remaining = iterations - 1;  // -1 because first iteration starts now
        p.loop_stack.push_back(frame);
        
        // Jump to loop body start
        p.current_instruction = loop_start;
        
        // Reset delay for loop body execution
        p.delay_ticks_left = config.delaysPerExec;
        return;  // Don't increment instruction counter (we already set it)
    }

    // Move to next instruction
    p.current_instruction++;
    
    // Check if we've reached the end of a FOR loop body
    if (!p.loop_stack.empty()) {
        LoopStruct& frame = p.loop_stack.back();
        
        if (p.current_instruction > frame.loop_end) {
            // We've finished one iteration of the loop body
            if (frame.iterations_remaining > 0) {
                // More iterations needed - jump back to loop start
                frame.iterations_remaining--;
                p.current_instruction = frame.loop_start;
            } else {
                // Loop finished - pop frame and continue
                p.loop_stack.pop_back();
            }
        }
    }
    
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
        std::this_thread::sleep_for(std::chrono::milliseconds(CPU_TICK_DELAY_MS));
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