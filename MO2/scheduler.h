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
#include <cstdint>

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
    READY,              ///< In ready queue, waiting for CPU
    RUNNING,            ///< Currently executing on a CPU core
    SLEEPING,           ///< Blocked, waiting for timer to expire
    FINISHED,           ///< All instructions completed
    MEMORY_VIOLATED     ///< Memory access violation detected
};

/**
 * @struct Instruction
 * @brief Single instruction in a process
 * 
 * Supported operations:
 * PRINT, DECLARE, ADD, SUBTRACT, FOR, SLEEP
 * Extended:
 * READ, WRITE
 */
struct Instruction {
    std::string op;                      ///< Operation name
    std::vector<std::string> args;       ///< Operands
};

/**
 * @struct LoopStruct
 * @brief Tracks state of a single FOR loop iteration
 */
struct LoopStruct {
    uint32_t loop_start;        ///< Instruction index where loop body starts
    uint32_t loop_end;          ///< Instruction index where loop body ends
    int iterations_remaining;   ///< Remaining iterations
};

/**
 * @struct Process
 * @brief Process control block (PCB)
 * 
 * Variables are uint16 and clamped to 0–65535.
 * Symbol table size is limited to 64 bytes.
 */
struct Process {
    int id;                              ///< PID
    std::string name;                    ///< Process name
    ProcessState state;                  ///< Current process state
    uint64_t sleep_until_tick;           ///< Wake-up tick for SLEEP
    uint32_t total_instructions;         ///< Total instruction count
    uint32_t current_instruction;        ///< Current instruction index
    uint32_t quantum_ticks_left;         ///< Remaining RR quantum
    uint32_t delay_ticks_left;           ///< Execution delay ticks


    uint32_t memory_size;                ///< Total process memory (bytes)
    uint32_t symbol_table_bytes_used;    ///< Bytes used in symbol table (max 64)

    // Symbol table: variable name -> uint16 value
    std::unordered_map<std::string, int> memory;

    // Simulated process memory for READ/WRITE (address -> uint16)
    std::unordered_map<uint32_t, uint16_t> data_memory;

    // Execution log (instructions executed, faults)
    std::vector<std::string> exec_log;

    // ======================================================================

    std::vector<Instruction> instructions; ///< Instruction list
    std::vector<LoopStruct> loop_stack;     ///< FOR-loop stack

    /**
     * @brief Constructor
     */
    Process(int pid, std::string pname, uint32_t total_ins, uint32_t mem_size = 1024)
        : id(pid),
          name(std::move(pname)),
          state(ProcessState::READY),
          sleep_until_tick(0),
          total_instructions(total_ins),
          current_instruction(0),
          quantum_ticks_left(0),
          delay_ticks_left(0),
          memory_size(mem_size),
          symbol_table_bytes_used(0) {}
};

// ============================================================================
// External global state (defined in main.cpp)
// ============================================================================
extern Config config;
extern bool isInitialized;
extern bool verboseMode;

// ============================================================================
// Scheduler state (defined in scheduler.cpp)
// ============================================================================
extern std::atomic<uint64_t> global_cpu_tick;
extern std::atomic<bool> is_generating_processes;
extern std::atomic<int> next_process_id;

extern std::mutex queue_mutex;
extern std::list<Process> ready_queue;
extern std::list<Process> sleeping_queue;
extern std::list<Process> finished_queue;
extern std::vector<std::optional<Process>> cpu_cores;

// ============================================================================
// Scheduler interface
// ============================================================================
void start_scheduler_thread();
void start_process_generation();
void stop_process_generation();

/**
 * @brief Execute one instruction of a process
 * @param p Process reference
 * @param current_tick Current global CPU tick
 */
void execute_instruction(Process& p, uint64_t current_tick);
