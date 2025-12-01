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
#include <sstream>
#include <cctype>

// External references from main.cpp
extern Config config;
extern bool isInitialized;
extern bool verboseMode;

// ============================================================================
// Global scheduler state
// ============================================================================
constexpr int UINT16_MAX_VALUE = 65535;                 ///< Maximum value for uint16 variables
constexpr int UINT16_MIN_VALUE = 0;                     ///< Minimum value for uint16 variables
constexpr int MAX_FOR_LOOP_DEPTH = 3;                   ///< Maximum FOR loop nesting depth per specs pg. 3
constexpr int NUM_INSTRUCTION_TYPES = 7;                ///< Num...uction types (PRINT, DECLARE, ADD, SUBTRACT, SLEEP, READ, WRITE)
constexpr int NUM_INSTRUCTION_TYPES_WITH_FOR = 6;       ///< Number of instruction types including FOR
constexpr int FOR_LOOP_PROBABILITY = 10;                ///< 1 in N chance to generate a FOR loop
constexpr int MIN_FOR_ITERATIONS = 2;                   ///< Minimum FOR loop iterations
constexpr int MAX_FOR_ITERATIONS = 5;                   ///< Maximum FOR loop iterations
constexpr int MIN_FOR_BODY_SIZE = 2;                    ///< Minimum instructions in FOR loop body
constexpr int MAX_FOR_BODY_SIZE = 5;                    ///< Maximum instructions in FOR loop body
constexpr int PROCESS_NAME_PADDING_THRESHOLD = 10;      ///< Process IDs below this get zero-padded (p01, p02, etc.)
constexpr int MAX_DECLARE_VALUE = 100;                  ///< Maximum random value for DECLARE instruction (0-99)
constexpr int MAX_ARITHMETIC_OPERAND = 50;              ///< Maximum random value for ADD/SUBTRACT operands (0-49)
constexpr int MIN_SLEEP_TICKS = 1;                      ///< Minimum sleep duration in CPU ticks
constexpr int MAX_SLEEP_TICKS = 10;                     ///< Maximum sleep duration in CPU ticks
constexpr int PROBABILITY_DENOMINATOR = 2;              ///< Denominator for 50% probability checks
constexpr int MAX_MEMORY_SIZE = 4096;                   ///< Max address space for auto-generated READ/WRITE
constexpr uint32_t SYMBOL_TABLE_BYTES = 64;             ///< Fixed symbol-table size in bytes
constexpr uint32_t BYTES_PER_UINT16 = 2;                ///< Size of one uint16 variable in bytes
constexpr int REQUIRED_OPERANDS_FOR_ARITHMETIC = 3;     ///< Number of operands required for ADD/SUBTRACT
constexpr int CPU_TICK_DELAY_MS = 100;                  ///< Real-time delay per CPU tick (in ms)

std::atomic<uint64_t> global_cpu_tick(0);               ///< Global CPU tick counter
std::atomic<bool> is_generating_processes(false);       ///< True when scheduler-start is active
std::atomic<int> next_process_id(1);                    ///< Next process ID to assign
std::atomic<uint64_t> total_active_ticks(0);            ///< Total CPU ticks spent executing processes
std::atomic<uint64_t> total_idle_ticks(0);              ///< Total CPU ticks spent idle

std::mutex queue_mutex;                                 ///< Protects all queues and cpu_cores

std::list<Process> ready_queue;                         ///< Processes waiting for CPU
std::list<Process> sleeping_queue;                      ///< Processes blocked on SLEEP
std::list<Process> finished_queue;                      ///< Completed processes

std::vector<std::optional<Process>> cpu_cores;          ///< Per-core running process

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
 * @brief Generate a random Hex Address string (e.g., "0x1A4")
 */
std::string generate_hex_address(int max_mem) {
    int addr = rand() % max_mem;
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << addr;
    return ss.str();
}

/**
 * @brief Generate process name with zero-padding (e.g., p01, p02, ..., p10, p11)
 */
std::string generate_process_name(int pid) {
    std::string padding = (pid < PROCESS_NAME_PADDING_THRESHOLD) ? "0" : "";
    return "p" + padding + std::to_string(pid);
}

/**
 * @brief Append a message to the per-process execution log.
 */
void log_event(Process& p, uint64_t tick, const std::string& msg) {
    std::ostringstream oss;
    oss << "[" << tick << "] " << msg;
    p.exec_log.push_back(oss.str());

    // Optional: cap log size to avoid unbounded growth
    if (p.exec_log.size() > 500) {
        p.exec_log.erase(p.exec_log.begin());
    }
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
    // Example: "Result: +x" becomes "Result: 42" if x=42
    while ((pos = message.find('+', pos)) != std::string::npos) {
        // Extract variable name after '+'
        size_t varStart = pos + 1;  // Skip the '+' character
        size_t varEnd = varStart;
        
        // Find the end of the variable name (alphanumeric + underscore)
        // Stops at first non-identifier character (space, comma, etc.)
        while (varEnd < message.length() && 
               (std::isalnum(static_cast<unsigned char>(message[varEnd])) || message[varEnd] == '_')) {
            varEnd++;
        }
        
        // Only process if we found a valid identifier after '+'
        if (varEnd > varStart) {
            std::string varName = message.substr(varStart, varEnd - varStart);
            
            // Get variable value (auto-initialize to 0 if not declared)
            if (p.memory.find(varName) == p.memory.end()) {
                p.memory[varName] = 0;  // Auto-declare as per specs pg. 3
            }
            int varValue = p.memory[varName];
            
            // Replace entire "+varName" substring with numeric value
            // Example: "+x" (3 chars) replaced with "42" (2 chars)
            message.replace(pos, varEnd - pos, std::to_string(varValue));
        }
        
        // Move to next position to search for more '+' patterns
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
 * @brief Ensure a variable has space in the 64-byte symbol table.
 *        Returns false if the table is already full.
 */
bool ensure_symbol_table_slot(Process& p, const std::string& varName) {
    // Already present
    if (p.memory.find(varName) != p.memory.end()) {
        return true;
    }

    // Check if there is enough space for one more uint16 (2 bytes)
    if (p.symbol_table_bytes_used + BYTES_PER_UINT16 > SYMBOL_TABLE_BYTES) {
        if (verboseMode) {
            std::cout << "[" << p.name << "] WARNING: symbol table full, ignoring variable '"
                      << varName << "'\n";
        }
        return false;
    }

    p.symbol_table_bytes_used += BYTES_PER_UINT16;
    p.memory[varName] = 0; // initialize to 0
    return true;
}

/**
 * @brief Get value of an operand (variable or literal)
 * @param operand String containing variable name or numeric literal
 * @param p Process with memory context
 * @return Resolved integer value
 */
int get_operand_value(const std::string& operand, Process& p) {
    if (operand.empty()) return 0;

    // Check if it's a numeric literal (e.g., "42" or "-5")
    // Must start with digit, or '-' followed by at least one digit
    if (std::isdigit(static_cast<unsigned char>(operand[0])) ||
        (operand[0] == '-' && operand.length() > 1)) {
        return std::stoi(operand);
    }
    
    // It's a variable - allocate symbol-table slot if possible
    if (!ensure_symbol_table_slot(p, operand)) {
        // Symbol table full; treat as 0 but do not store
        return 0;
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
    if (var_pool.empty() || rand() % PROBABILITY_DENOMINATOR == 0) {
        // Generate numeric literal
        return std::to_string(rand() % max_literal);
    } else {
        // Pick random variable from pool
        return var_pool[rand() % var_pool.size()];
    }
}

/**
 * @brief Execute arithmetic operation (ADD or SUBTRACT)
 * @param p Process to execute on
 * @param ins Instruction with operands
 * @param is_add True for addition, false for subtraction
 */
void execute_arithmetic(Process& p, const Instruction& ins, bool is_add) {
    // ADD/SUBTRACT require 3 operands: var1, var2, var3/numeric
    if (ins.args.size() < REQUIRED_OPERANDS_FOR_ARITHMETIC) {
        if (verboseMode)
            std::cout << "[" << p.name << "] ERROR: " << ins.op 
                      << " requires 3 operands\n";
        return;
    }
    
    std::string var1 = ins.args[0];

    // Ensure destination variable has space in symbol table
    if (!ensure_symbol_table_slot(p, var1)) {
        return; // symbol table full, ignore operation
    }

    int value2 = get_operand_value(ins.args[1], p);
    int value3 = get_operand_value(ins.args[2], p);
    
    int result = is_add ? (value2 + value3) : (value2 - value3);
    p.memory[var1] = clamp_to_uint16(result);
}

/**
 * @brief Parse a hexadecimal address token (expects 0x prefix).
 * @return true on success, false on invalid hex.
 */
bool parse_hex_address(const std::string& token, uint32_t& out) {
    if (token.size() < 3) return false;
    if (!(token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))) {
        return false;
    }

    for (size_t i = 2; i < token.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(token[i]))) {
            return false;
        }
    }

    try {
        unsigned long val = std::stoul(token, nullptr, 16);
        out = static_cast<uint32_t>(val);
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * @brief Parse semicolon-separated command string into instruction vector
 * @param commands String containing commands separated by semicolons
 * @return Vector of parsed instructions
 * 
 * This function is used by the screen -c command to parse user-provided
 * instruction strings. Each instruction is separated by a semicolon.
 * 
 * Example: "DECLARE x 5; ADD x x 1; PRINT +x"
 * Results in 3 instructions:
 *   1. DECLARE ["x", "5"]
 *   2. ADD ["x", "x", "1"]
 *   3. PRINT ["+x"]
 */
std::vector<Instruction> parse_command_string(const std::string& commands) {
    std::vector<Instruction> result;
    
    if (commands.empty()) {
        return result;
    }
    
    // Split by semicolons
    size_t start = 0;
    size_t end = 0;
    
    while (end != std::string::npos) {
        end = commands.find(';', start);
        
        // Extract one command (everything between start and end/EOF)
        std::string cmd = (end == std::string::npos) 
            ? commands.substr(start) 
            : commands.substr(start, end - start);
        
        // Trim leading/trailing whitespace
        size_t first = cmd.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) {
            // Empty or whitespace-only command, skip
            start = end + 1;
            continue;
        }
        
        size_t last = cmd.find_last_not_of(" \t\n\r");
        cmd = cmd.substr(first, last - first + 1);
        
        if (cmd.empty()) {
            start = end + 1;
            continue;
        }
        
        // Parse the command into tokens (split by spaces)
        Instruction ins;
        std::vector<std::string> tokens;
        
        size_t token_start = 0;
        size_t token_end = 0;
        
        while (token_end != std::string::npos) {
            token_end = cmd.find(' ', token_start);
            
            std::string token = (token_end == std::string::npos)
                ? cmd.substr(token_start)
                : cmd.substr(token_start, token_end - token_start);
            
            // Skip empty tokens (consecutive spaces)
            if (!token.empty()) {
                tokens.push_back(token);
            }
            
            if (token_end != std::string::npos) {
                token_start = token_end + 1;
            }
        }
        
        // First token is the operation
        if (!tokens.empty()) {
            ins.op = tokens[0];
            
            // Remaining tokens are arguments
            for (size_t i = 1; i < tokens.size(); ++i) {
                ins.args.push_back(tokens[i]);
            }
            
            result.push_back(ins);
        }
        
        // Move to next command
        if (end != std::string::npos) {
            start = end + 1;
        }
    }
    
    return result;
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

    // Create process and reserve memory
    Process p(pid, pname, num_instructions);

    // Prepopulate some variables for use in instructions
    std::vector<std::string> var_pool = { "x", "y", "z", "counter" };

    // Generate instruction list
    for (uint32_t i = 0; i < num_instructions; ++i) {
        Instruction ins;
        
        // Check if we can generate a FOR loop (must have enough remaining instructions)
        uint32_t remaining_instructions = num_instructions - i - 1;
        bool can_generate_for = (remaining_instructions >= MIN_FOR_BODY_SIZE) &&
                                (rand() % FOR_LOOP_PROBABILITY == 0);

        if (can_generate_for) {
            // Generate FOR loop header
            ins.op = "FOR";
            
            // Random iteration count
            int iterations = random_in_range(MIN_FOR_ITERATIONS, MAX_FOR_ITERATIONS);
            ins.args.push_back(std::to_string(iterations));
            
            // Random block size (clamped to remaining instructions)
            int max_block = std::min(static_cast<int>(remaining_instructions), MAX_FOR_BODY_SIZE);
            int block_size = random_in_range(MIN_FOR_BODY_SIZE, max_block);
            ins.args.push_back(std::to_string(block_size));
        } else {
            // Generate regular instruction (PRINT, DECLARE, ADD, SUBTRACT, SLEEP, READ, WRITE)
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
                case 5: //READ
                    ins.op = "READ";
                    ins.args.push_back(var_pool[rand() % var_pool.size()]); // var
                    ins.args.push_back(generate_hex_address(MAX_MEMORY_SIZE)); 
                    break;
                case 6: //WRITE
                    ins.op = "WRITE";
                    ins.args.push_back(generate_hex_address(MAX_MEMORY_SIZE)); 
                    ins.args.push_back(generate_random_operand(var_pool, MAX_DECLARE_VALUE));
                    break;
            }
        }
        
        p.instructions.push_back(ins);
    }


    // Add process to ready queue
    std::lock_guard<std::mutex> lock(queue_mutex);
    ready_queue.push_back(std::move(p));
}

// ============================================================================
// Queue management
// ============================================================================

/**
  * @brief Move processes from sleeping_queue to ready_queue when their sleep expires
 */
void check_sleeping() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    uint64_t current_tick = global_cpu_tick.load();

    auto it = sleeping_queue.begin();
    while (it != sleeping_queue.end()) {
        if (current_tick >= it->sleep_until_tick) {
            // Move process back to ready state
            if (verboseMode)
                std::cout << "\n[Scheduler] Process " << it->name 
                          << " is WAKING UP." << std::endl;

            it->state = ProcessState::READY;
            ready_queue.push_back(std::move(*it));
            it = sleeping_queue.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * @brief Dispatch ready processes to available CPU cores
 * 
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

            // Set process state and initialize quantum
            p.state = ProcessState::RUNNING;

            // Set quantum for Round Robin
            if (config.scheduler == "rr") {
                p.quantum_ticks_left = config.quantumCycles;
            }

            if (verboseMode) 
                std::cout << "\n[Scheduler] DISPATCHING " << p.name 
                          << " to CPU " << i << "." << std::endl;

            // Assign to CPU core
            cpu_cores[i] = std::move(p);

            // Stop if there are no more ready processes
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
  * @brief Execute one CPU tick across all cores
  *  
  * Handles RR quantum, sleep transitions, and process completion.
 */
void execute_cpu_tick() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    uint64_t current_tick = global_cpu_tick.load();

    for (int i = 0; i < cpu_cores.size(); ++i) {
        if (cpu_cores[i].has_value()) {
            Process& p = *cpu_cores[i];

            // TODO: Uncomment when memory manager is ready
            
            // bool is_resident = is_page_resident(p.id, p.current_instruction); 
            // if (!is_resident) {
            //     request_page(p.id, p.current_instruction);
            //     // Do NOT execute instruction.
            //     // Do NOT decrement quantum (stalling).
            //     continue; 
            // }

            // Execute one instruction
            execute_instruction(p, current_tick);

            if (p.state == ProcessState::MEMORY_VIOLATED) {
                finished_queue.push_back(std::move(p));
                cpu_cores[i].reset();
                continue;
            }

            // Check if process finished or went to sleep (state changed by execute_instruction)
            if (p.state == ProcessState::FINISHED) {
                finished_queue.push_back(std::move(p));
                cpu_cores[i].reset();
                continue;
            }
            
            if (p.state == ProcessState::SLEEPING) {
                sleeping_queue.push_back(std::move(p));
                cpu_cores[i].reset();
                continue;
            }

            // Handle RR quantum for RUNNING process
            if (config.scheduler == "rr") {
                // Decrement quantum per tick
                if (p.quantum_ticks_left > 0) {
                    p.quantum_ticks_left--;
                }

                // If quantum expires, preempt the process
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
 * Supported instructions:
 * - PRINT <message>: Output message to console (supports variable concatenation: +varname)
 * - DECLARE <var> <value>: Initialize variable
 * - ADD <var1> <var2/value> <var3/value>: var1 = var2/value + var3/value
 * - SUBTRACT <var1> <var2/value> <var3/value>: var1 = var2/value - var3/value
 * - SLEEP <ticks>: Block process for <ticks> CPU ticks (sets state to SLEEPING)
 * - READ <var> <address>: Read from memory address into variable
 * - WRITE <address> <var/value>: Write variable or value to memory address
 * - FOR <iterations> <block_size>: Loop control
 * 
 * Variables are stored in p.memory (uint16 clamped to [0, 65535]).
 * Undeclared variables auto-initialize to 0.
 * 
 * When process completes, sleeps, or encounters memory violation, only the state is updated.
 * Caller (execute_cpu_tick) is responsible for moving process to appropriate queue.
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
        return;  // Caller will move to finished_queue and reset core
    }

    // Fetch current instruction
    Instruction& ins = p.instructions[p.current_instruction];

    // Log the instruction before execution (for process-smi)
    {
        std::ostringstream oss;
        oss << "EXEC " << ins.op;
        for (const auto& a : ins.args) {
            oss << " " << a;
        }
        log_event(p, current_tick, oss.str());
    }

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
        if (ins.args.size() >= 2) {
            const std::string& varName = ins.args[0];
            int value = std::stoi(ins.args[1]);

            if (ensure_symbol_table_slot(p, varName)) {
                p.memory[varName] = clamp_to_uint16(value);
            }
        }
    }
    else if (ins.op == "ADD") {
        execute_arithmetic(p, ins, true);
    }
    else if (ins.op == "SUBTRACT") {
        execute_arithmetic(p, ins, false);
    }
    else if (ins.op == "SLEEP") {
        // Block process for specified ticks
        if (!ins.args.empty()) {
            p.state = ProcessState::SLEEPING;
            p.sleep_until_tick = current_tick + std::stoi(ins.args[0]);
            p.current_instruction++;  // Move to next instruction before sleeping
            return;  // Caller will move to sleeping_queue and reset core
        }
    }
    else if (ins.op == "READ") {
        // READ <var> <hex_addr>
        if (ins.args.size() < 2) {
            if (verboseMode)
                std::cout << "[" << p.name << "] ERROR: READ requires 2 arguments (var, hex_addr)\n";
        } else {
            const std::string& varName = ins.args[0];
            const std::string& addrToken = ins.args[1];
            uint32_t addr = 0;

            // Validate hex address and bounds
            if (!parse_hex_address(addrToken, addr) || addr >= p.memory_size) {
                log_event(p, current_tick, "FAULT: invalid READ address " + addrToken);
                if (verboseMode)
                    std::cout << "[" << p.name << "] MEMORY VIOLATION on READ at "
                              << addrToken << " (mem size " << p.memory_size << ")\n";
                p.state = ProcessState::MEMORY_VIOLATED;
                return;
            }

            // TODO: Uncomment when memory manager is ready
            // // Memory Manager Integration Hook:
            // // Check if the page containing this address is resident in physical memory
            
            // bool is_resident = MemoryManager::is_page_resident(p.id, addr);
            // if (!is_resident) {
            //     // Page fault - request page from disk
            //     MemoryManager::request_page(p.id, addr);
            //     // Do NOT execute instruction - process stalls
            //     // Do NOT increment current_instruction
            //     // Quantum should NOT be decremented (process is blocked)
            //     return;
            // }

            // Execute the READ operation
            if (ensure_symbol_table_slot(p, varName)) {
                uint16_t value = 0;
                auto it = p.data_memory.find(addr);
                if (it != p.data_memory.end()) {
                    value = it->second;
                }
                p.memory[varName] = clamp_to_uint16(value);
            }
        }
    }
    else if (ins.op == "WRITE") {
        // WRITE <hex_addr> <var/value>
        if (ins.args.size() < 2) {
            if (verboseMode)
                std::cout << "[" << p.name << "] ERROR: WRITE requires 2 arguments (hex_addr, var/value)\n";
        } else {
            const std::string& addrToken = ins.args[0];
            const std::string& valueToken = ins.args[1];
            uint32_t addr = 0;

            // Validate hex address and bounds
            if (!parse_hex_address(addrToken, addr) || addr >= p.memory_size) {
                log_event(p, current_tick, "FAULT: invalid WRITE address " + addrToken);
                if (verboseMode)
                    std::cout << "[" << p.name << "] MEMORY VIOLATION on WRITE at "
                              << addrToken << " (mem size " << p.memory_size << ")\n";
                p.state = ProcessState::MEMORY_VIOLATED;
                return;
            }

            // TODO: Uncomment when memory manager is ready
            // Memory Manager Integration Hook:
            // Check if the page containing this address is resident in physical memory
            // 
            // bool is_resident = MemoryManager::is_page_resident(p.id, addr);
            // if (!is_resident) {
            //     // Page fault - request page from disk
            //     MemoryManager::request_page(p.id, addr);
            //     // Do NOT execute instruction - process stalls
            //     // Do NOT increment current_instruction
            //     // Quantum should NOT be decremented (process is blocked)
            //     return;
            // }

            // Execute the WRITE operation
            int raw = get_operand_value(valueToken, p);
            uint16_t value = static_cast<uint16_t>(clamp_to_uint16(raw));
            p.data_memory[addr] = value;
        }
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
    // Loop stack tracks nested FOR loops (up to MAX_FOR_LOOP_DEPTH levels)
    if (!p.loop_stack.empty()) {
        LoopStruct& frame = p.loop_stack.back();  // Get innermost loop
        
        // Check if we've executed past the last instruction in the loop body
        if (p.current_instruction > frame.loop_end) {
            // We've finished one iteration of the loop body
            if (frame.iterations_remaining > 0) {
                // More iterations needed - jump back to loop start
                frame.iterations_remaining--;
                p.current_instruction = frame.loop_start;
            } else {
                // All iterations complete - exit this loop level
                // If this was a nested loop, control returns to outer loop
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
            
            // Count how many CPU cores are actively running processes
            // This is used for CPU utilization metrics
            int active_cores = 0;
            {
                // Lock to safely access cpu_cores vector
                std::lock_guard<std::mutex> lock(queue_mutex);
                for(const auto& core : cpu_cores) {
                    if (core.has_value()) {
                        active_cores++;
                    }
                }
            }
            
            // Update CPU utilization statistics
            // total_active_ticks tracks sum of all core-ticks spent executing
            // total_idle_ticks tracks sum of all core-ticks spent idle
            total_active_ticks += active_cores;
            total_idle_ticks += (config.numCPU - active_cores);

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
    std::thread schedulerThread(scheduler_loop);
    schedulerThread.detach();
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