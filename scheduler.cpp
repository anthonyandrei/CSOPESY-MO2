#include "scheduler.h"
#include "config.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>

extern Config config;
extern bool isInitialized;
extern bool verboseMode;


std::atomic<uint64_t> global_cpu_tick(0);
std::atomic<bool> is_generating_processes(false);
std::atomic<int> next_process_id(1);

std::mutex queue_mutex;

std::list<Process> ready_queue;
std::list<Process> sleeping_queue;
std::list<Process> finished_queue;

std::vector<std::optional<Process>> cpu_cores;


void generate_new_process() {
    // config settings
    uint32_t min_ins = config.minIns;
    uint32_t max_ins = config.maxIns;

    // random count
    uint32_t num_instructions = min_ins;
    if (max_ins > min_ins) {
        num_instructions = (rand() % (max_ins - min_ins + 1)) + min_ins;
    }

    //generates the next process name here
    int pid = next_process_id++;
    std::string pname = std::string("p") + (pid < 10 ? "0" : "") + std::to_string(pid);

    if (verboseMode) {
        std::cout << "\n[Scheduler] Generating process " << pname
            << " (" << num_instructions << " instructions)." << std::endl;
    }

    std::lock_guard<std::mutex> lock(queue_mutex);
    ready_queue.emplace_back(pid, pname, num_instructions); //mutex auto unlocks
}

void check_sleeping() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    uint64_t current_tick = global_cpu_tick.load();

    auto it = sleeping_queue.begin();
    while (it != sleeping_queue.end()) {
        if (current_tick >= it->sleep_until_tick) {
            if (verboseMode) std::cout << "\n[Scheduler] Process " << it->name << " is WAKING UP." << std::endl;
            it->state = ProcessState::READY;
            ready_queue.push_back(std::move(*it));//move process to ready
            it = sleeping_queue.erase(it);//and remove from sleeping
        }
        else {
            ++it;
        }
    }
}

void dispatch_processes() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    if (ready_queue.empty()) {
        return;
    }

    for (int i = 0; i < cpu_cores.size(); ++i) {
        if (!cpu_cores[i].has_value()) {


            Process p = std::move(ready_queue.front());
            ready_queue.pop_front();
            //FCFS here ^
            p.state = ProcessState::RUNNING;

            // if it's RR we instead set its quantum
            if (config.scheduler == "rr") {
                p.quantum_ticks_left = config.quantumCycles;
            }

            if (verboseMode) std::cout << "\n[Scheduler] DISPATCHING " << p.name << " to CPU " << i << "." << std::endl;
            cpu_cores[i] = std::move(p);

            if (ready_queue.empty()) {
                break;
            }
        }
    }
}


void execute_cpu_tick() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    uint64_t current_tick = global_cpu_tick.load();

    for (int i = 0; i < cpu_cores.size(); ++i) {
        if (cpu_cores[i].has_value()) {

            Process& p = *cpu_cores[i];

            //this should be mainly for task 2
            // just simulating a sleep instruction. sampling an instruction to sleep 30 every 50
            p.current_instruction++;

            if (p.current_instruction % 50 == 0 && p.current_instruction > 0 && p.current_instruction < p.total_instructions) {
                if (verboseMode) std::cout << "\n[Scheduler] Process " << p.name << " is SLEEPING." << std::endl;
                p.state = ProcessState::SLEEPING;
                p.sleep_until_tick = current_tick + 30;
                sleeping_queue.push_back(std::move(p));
                cpu_cores[i].reset();
                continue;
            }

            if (p.current_instruction >= p.total_instructions) {
                if (verboseMode) std::cout << "\n[Scheduler] Process " << p.name << " has FINISHED." << std::endl;
                p.state = ProcessState::FINISHED;
                finished_queue.push_back(std::move(p));
                cpu_cores[i].reset();
                continue;
            }

            //checks if it's round robin
            if (config.scheduler == "rr") {
                p.quantum_ticks_left--;
                if (p.quantum_ticks_left == 0) {
                    if (verboseMode) std::cout << "\n[Scheduler] Process " << p.name << " PREEMPTED (RR)." << std::endl;
                    p.state = ProcessState::READY;
                    ready_queue.push_back(std::move(p));
                    cpu_cores[i].reset();
                }
            }
        }
    }
}


void scheduler_loop() {
    uint64_t last_generation_tick = 0;

    while (true) {
        if (isInitialized) {

            global_cpu_tick++;
            uint64_t current_tick = global_cpu_tick.load();

            if (is_generating_processes.load() &&
                (current_tick - last_generation_tick >= config.batchProcessFreq))
            {
                last_generation_tick = current_tick;
                generate_new_process();
            }


            check_sleeping();
            // fcfs rr sleep finished
            execute_cpu_tick();

            //free up cpus
            dispatch_processes();
        }

        //this is just to simulate a cpu tick and not giga use our cpu
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}



void start_scheduler_thread() {
    std::thread(scheduler_loop).detach();
}
void start_process_generation() {
    is_generating_processes = true;
}
void stop_process_generation() {
    is_generating_processes = false;
}