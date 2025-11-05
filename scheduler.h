/**
 * @file scheduler.h
 * @brief Process scheduler and management declarations
 * 
 * Defines process structures, scheduling queues, and scheduler functions.
 * Implements CPU tick simulation and process lifecycle management.
 * See CSOPESY - MO1 specs.txt pages 2-3 for process behavior details.
 */

#pragma once
#include "config.h"
#include <string>
#include <list>
#include <vector>
#include <atomic>
#include <optional>
#include <mutex>
#include <unordered_map>

/**
 * @enum ProcessState
 * @brief Lifecycle states of a process
 * 
 * State transitions:
 * READY -> RUNNING (dispatch)
 * RUNNING -> SLEEPING (SLEEP instruction)
 * RUNNING -> FINISHED (all instructions complete)
 * RUNNING -> READY (RR preemption)
 * SLEEPING -> READY (wake up after sleep_until_tick)
 */
enum class ProcessState {
    READY,      ///< In ready queue, waiting for CPU
    RUNNING,    ///< Currently executing on a CPU core
    SLEEPING,   ///< Blocked, waiting for timer to expire
    FINISHED    ///< All instructions completed
};

/**
 * @struct Instruction
 * @brief Single instruction in a process
 * 
 * Supported operations (specs pg. 2-3):
 * - PRINT <message>: Supports variable concatenation (+varname)
 * - DECLARE <var> <value>
 * - ADD <var1> <var2/value> <var3/value>: var1 = var2/value + var3/value
 * - SUBTRACT <var1> <var2/value> <var3/value>: var1 = var2/value - var3/value
 * - SLEEP <ticks>
 * - FOR <count>
 */
struct Instruction {
    std::string op;                      ///< Operation name (PRINT, DECLARE, etc.)
    std::vector<std::string> args;       ///< Operands (variable names, values, messages)
};

/**
 * @struct Process
 * @brief Process control block
 * 
 * Represents a single process with its instruction list, variables, and state.
 * Variables are uint16 and clamped to [0, 65535] per specs pg. 3.
 */
struct Process {
    int id;                              ///< Unique process ID
    std::string name;                    ///< Human-readable name (p01, p02, ...)
    ProcessState state;                  ///< Current lifecycle state
    uint64_t sleep_until_tick;           ///< Wake-up time for SLEEPING processes
    uint32_t total_instructions;         ///< Total instruction count
    uint32_t current_instruction;        ///< Index of next instruction to execute
    uint32_t quantum_ticks_left;         ///< Remaining time slice for RR scheduling
    uint32_t delay_ticks_left;           ///< Busy-wait delay before next instruction
    int for_loop_depth;                  ///< Current FOR loop nesting depth (max 3)

    std::vector<Instruction> instructions;           ///< Instruction list
    std::unordered_map<std::string,int> memory;      ///< Variable storage (name -> value)

    /**
     * @brief Construct a new Process
     * @param pid Process ID
     * @param pname Process name
     * @param total_ins Total instruction count
     */
    Process(int pid, std::string pname, uint32_t total_ins)
        : id(pid), name(std::move(pname)), state(ProcessState::READY),
        sleep_until_tick(0), total_instructions(total_ins),
        current_instruction(0), quantum_ticks_left(0), delay_ticks_left(0),
        for_loop_depth(0) {
    }
};

// ============================================================================
// External declarations (defined in main.cpp)
// ============================================================================
extern Config config;           ///< Global configuration
extern bool isInitialized;      ///< True after successful 'initialize' command
extern bool verboseMode;        ///< Enable debug output

// ============================================================================
// Scheduler state (defined in scheduler.cpp)
// ============================================================================
extern std::atomic<uint64_t> global_cpu_tick;           ///< Global CPU tick counter
extern std::atomic<bool> is_generating_processes;       ///< True when scheduler-start is active
extern std::atomic<int> next_process_id;                ///< Next process ID to assign
extern std::mutex queue_mutex;                          ///< Protects all queues and cpu_cores
extern std::list<Process> ready_queue;                  ///< Processes waiting for CPU
extern std::list<Process> sleeping_queue;               ///< Processes blocked on SLEEP
extern std::list<Process> finished_queue;               ///< Completed processes
extern std::vector<std::optional<Process>> cpu_cores;   ///< Per-core running process (size = numCPU)

// ============================================================================
// Scheduler functions
// ============================================================================

/**
 * @brief Start the background scheduler thread
 * 
 * Spawns a detached thread running scheduler_loop().
 * Called once after successful initialization.
 */
void start_scheduler_thread();

/**
 * @brief Enable periodic process generation
 * 
 * Sets is_generating_processes flag. Processes are created
 * every batchProcessFreq ticks in scheduler_loop().
 */
void start_process_generation();

/**
 * @brief Disable periodic process generation
 * 
 * Clears is_generating_processes flag. Already-created
 * processes continue executing.
 */
void stop_process_generation();

/**
 * @brief Execute one instruction of a process
 * @param p Process to execute
 * @param current_tick Current global CPU tick
 * 
 * Executes p.instructions[p.current_instruction] and updates state.
 * May move process to sleeping_queue or finished_queue.
 */
void execute_instruction(Process& p, uint64_t current_tick);