#pragma once
#include "config.h"
#include <string>
#include <list>
#include <vector>
#include <atomic>
#include <optional>
#include <mutex>

// main and scheduler will use the struct from here instead
enum class ProcessState {
    READY,
    RUNNING,
    SLEEPING,
    FINISHED
};

struct Process {
    int id;
    std::string name;
    ProcessState state;
    uint64_t sleep_until_tick;
    uint32_t total_instructions;
    uint32_t current_instruction;
    uint32_t quantum_ticks_left;

    Process(int pid, std::string pname, uint32_t total_ins)
        : id(pid), name(std::move(pname)), state(ProcessState::READY),
        sleep_until_tick(0), total_instructions(total_ins),
        current_instruction(0), quantum_ticks_left(0) {
    }
};

// linker from main.cpp
extern Config config;
extern bool isInitialized;
extern bool verboseMode;

// scheduler.cpp
extern std::atomic<uint64_t> global_cpu_tick;
extern std::atomic<bool> is_generating_processes;
extern std::atomic<int> next_process_id;
extern std::mutex queue_mutex;
extern std::list<Process> ready_queue;
extern std::list<Process> sleeping_queue;
extern std::list<Process> finished_queue;
extern std::vector<std::optional<Process>> cpu_cores;

// funcs
void start_scheduler_thread();
void start_process_generation();
void stop_process_generation();