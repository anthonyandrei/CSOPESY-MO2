/**
 * @file main.cpp
 * @brief Main CLI shell and command interpreter for CSOPESY OS Emulator
 * 
 * Provides:
 * - Interactive command shell for system control
 * - Configuration loading from config.txt
 * - Process creation via screen commands
 * - System statistics and monitoring
 * - Integration with scheduler and memory manager
 */

#include "config.h"
#include "scheduler.h"
#include <iostream>
#include <string>
#include <sstream>
#include <utility>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <thread>
#include <atomic>
#include <vector>
#include <list>
#include <optional>
#include <mutex>
#include <cstdlib>
#include <ctime>

#include "memory_manager.h"

using namespace std;

// ============================================================================
// Global variables
// ============================================================================
Config config;              ///< System configuration loaded from config.txt
bool isInitialized = false; ///< True after successful 'initialize' command
bool verboseMode = true;    ///< Enable verbose logging output

// ============================================================================
// Utility helpers
// ============================================================================

/**
 * @brief Find process by name in all queues and CPU cores
 * @param name Process name to search for
 * @return Pointer to process if found, nullptr otherwise
 * 
 * Searches in order: ready_queue, sleeping_queue, cpu_cores, finished_queue
 */
Process* find_process(const std::string& name) {
    for (auto& p : ready_queue)
        if (p.name == name) return &p;

    for (auto& p : sleeping_queue)
        if (p.name == name) return &p;

    for (auto& core : cpu_cores)
        if (core.has_value() && core->name == name)
            return &core.value();

    for (auto& p : finished_queue)
        if (p.name == name) return &p;

    return nullptr;
}

/**
 * @brief Calculate CPU utilization statistics
 * @return Tuple of (cores_used, cores_available, utilization_percentage)
 * 
 * Counts how many CPU cores currently have running processes.
 * Utilization = (cores_used / total_cores) * 100
 */
tuple<int, int, double> calculate_cpu_utilization() {
    int used = 0;
    for (auto& c : cpu_cores)
        if (c.has_value()) used++;

    int available = static_cast<int>(cpu_cores.size()) - used;
    double utilization = cpu_cores.empty()
        ? 0.0
        : (static_cast<double>(used) / cpu_cores.size()) * 100.0;

    return { used, available, utilization };
}

/**
 * @brief Generate formatted list of all processes with their states
 * @return String containing process list (one per line)
 * 
 * Format: "processName [STATE]\n"
 * States: READY, RUNNING, SLEEPING, FINISHED
 */
string generate_process_list() {
    stringstream ss;
    for (auto& p : ready_queue)    ss << p.name << " [READY]\n";
    for (auto& c : cpu_cores)
        if (c.has_value())         ss << c->name << " [RUNNING]\n";
    for (auto& p : sleeping_queue) ss << p.name << " [SLEEPING]\n";
    for (auto& p : finished_queue) ss << p.name << " [FINISHED]\n";
    return ss.str();
}

/**
 * @brief Remove leading whitespace from string (in-place)
 * @param s String to trim
 */
void trimLeadingSpaces(string& s) {
    size_t i = 0;
    while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i) s.erase(0, i);
}

/**
 * @brief Parse command line into command and remaining arguments
 * @param input Full command line string
 * @return Pair of (command, remaining_arguments)
 * 
 * Example: "screen -s myprocess" -> {"screen", "-s myprocess"}
 */
pair<string, string> parseCommand(const string& input) {
    stringstream ss(input);
    string cmd, rest;
    ss >> cmd;
    getline(ss, rest);
    trimLeadingSpaces(rest);
    return { cmd, rest };
}

// ============================================================================
// UI / Help
// ============================================================================

void showGreeting() {
    cout << "=====================================\n";
    cout << "          CSOPESY OS Emulator        \n";
    cout << "=====================================\n";
    cout << "Type 'initialize' to start or 'help' for commands.\n\n";
}

void showHelp() {
    cout << "\nAvailable Commands\n";
    cout << "------------------\n";
    cout << "initialize\n";
    cout << "screen -s <name>\n";
    cout << "screen -c <name> <memsize> \"<instructions>\"\n";
    cout << "screen -r <name>\n";
    cout << "screen -ls\n";
    cout << "scheduler-start\n";
    cout << "scheduler-stop\n";
    cout << "report-util\n";
    cout << "exit\n\n";

    cout << "Inside screen:\n";
    cout << "  process-smi\n";
    cout << "  exit\n\n";
}

// ============================================================================
// Config Loader
// ============================================================================

/**
 * @brief Parse config.txt and populate global config struct
 * @param file Input file stream (config.txt)
 * 
 * Reads key-value pairs from config file:
 * - num-cpu <int>
 * - scheduler <string>
 * - quantum-cycles <uint32>
 * - batch-process-freq <uint32>
 * - min-ins <uint32>
 * - max-ins <uint32>
 * - delays-per-exec <uint32>
 * - max-overall-mem <uint32>
 * - mem-per-frame <uint32>
 * - min-mem-per-proc <uint32>
 * - max-mem-per-proc <uint32>
 * - replacement-policy <string>
 */
void initializeConfig(ifstream& file) {
    string key;
    while (file >> key) {
        if (key == "num-cpu")              file >> config.numCPU;
        else if (key == "scheduler")       file >> config.scheduler;
        else if (key == "quantum-cycles")  file >> config.quantumCycles;
        else if (key == "batch-process-freq") file >> config.batchProcessFreq;
        else if (key == "min-ins")         file >> config.minIns;
        else if (key == "max-ins")         file >> config.maxIns;
        else if (key == "delays-per-exec") file >> config.delaysPerExec;

        // Memory configuration
        else if (key == "max-overall-mem") file >> config.maxOverallMem;
        else if (key == "mem-per-frame")   file >> config.memPerFrame;
        else if (key == "min-mem-per-proc") file >> config.minMemPerProc;
        else if (key == "max-mem-per-proc") file >> config.maxMemPerProc;
        else if (key == "replacement-policy") file >> config.replacementPolicy;
        else {
            // Unknown key - skip value
            string dummy;
            file >> dummy;
        }
    }
}

/**
 * @brief Validate loaded configuration values
 * @param cfg Configuration to validate
 * @return true if config is valid, false otherwise
 * 
 * Checks:
 * - numCPU >= 1
 * - scheduler is "fcfs" or "rr"
 * - quantumCycles >= 1
 * - batchProcessFreq >= 1
 * - minIns >= 1 and maxIns >= minIns
 */

bool isValidConfig(const Config& cfg) {
    if (cfg.numCPU < 1) return false;
    if (cfg.scheduler != "fcfs" && cfg.scheduler != "rr") return false;
    if (cfg.quantumCycles < 1) return false;
    if (cfg.batchProcessFreq < 1) return false;
    if (cfg.minIns < 1 || cfg.maxIns < cfg.minIns) return false;
    return true;
}

/**
 * @brief Check if a number is a power of two
 * @param x Number to check
 * @return true if x is a power of 2, false otherwise
 * 
 * Uses bit manipulation: powers of 2 have only one bit set.
 * Example: 8 (1000) & 7 (0111) = 0
 */
bool isPowerOfTwo(uint32_t x) {
    return x && !(x & (x - 1));
}

// ============================================================================
// Screen Command
// ============================================================================

void handleScreenCommand(const string& param) {
    auto [sub, rest] = parseCommand(param);

    // ------------------------------------------------------------------------
    // screen -s (auto-generated instructions)
    // ------------------------------------------------------------------------
    if (sub == "-s") {
        Process p(next_process_id++, rest, 5);
        p.instructions = {
            { "DECLARE", { "x", "0" } },
            { "ADD", { "x", "x", "1" } },
            { "PRINT", { "x = +x" } }
        };

        lock_guard<mutex> lock(queue_mutex);
        ready_queue.push_back(move(p));
        cout << "Process " << rest << " created.\n";
    }

    // ------------------------------------------------------------------------
    // screen -c (user-defined instructions)
    // ------------------------------------------------------------------------
    else if (sub == "-c") {
        string pname;
        uint32_t memsize;

        stringstream ss(rest);
        ss >> pname >> memsize;

        if (!ss || memsize < 64 || memsize > 65536 || !isPowerOfTwo(memsize)) {
            cout << "invalid memory allocation\n";
            return;
        }

        string code;
        getline(ss, code);
        trimLeadingSpaces(code);

        if (code.size() < 2 || code.front() != '"' || code.back() != '"') {
            cout << "invalid command\n";
            return;
        }

        code = code.substr(1, code.size() - 2);

        vector<string> lines;
        string temp;
        stringstream cs(code);
        while (getline(cs, temp, ';')) {
            trimLeadingSpaces(temp);
            if (!temp.empty()) lines.push_back(temp);
        }

        if (lines.empty() || lines.size() > 50) {
            cout << "invalid command\n";
            return;
        }

        vector<Instruction> instructions;
        for (const auto& line : lines) {
            string op, restOp;
            stringstream ls(line);
            ls >> op;
            getline(ls, restOp);
            trimLeadingSpaces(restOp);

            Instruction ins;
            ins.op = op;

            if (op == "PRINT") {
                ins.args.push_back(restOp);
            } else {
                string arg;
                stringstream as(restOp);
                while (as >> arg) ins.args.push_back(arg);
            }

            // minimal operand validation
            bool valid = true;
            if (op == "DECLARE") valid = (ins.args.size() == 2);
            else if (op == "ADD" || op == "SUBTRACT") valid = (ins.args.size() == 3);
            else if (op == "SLEEP") valid = (ins.args.size() == 1);
            else if (op == "FOR") valid = (ins.args.size() == 2);
            else if (op == "READ" || op == "WRITE") valid = (ins.args.size() == 2);
            else if (op == "PRINT") valid = true;
            else valid = false;

            if (!valid) {
                cout << "invalid command\n";
                return;
            }

            instructions.push_back(ins);
        }

        Process p(next_process_id++, pname,
                  static_cast<uint32_t>(instructions.size()),
                  memsize);
        p.instructions = move(instructions);

        lock_guard<mutex> lock(queue_mutex);
        ready_queue.push_back(move(p));

        cout << "Process " << pname << " created.\n";
    }

    // ------------------------------------------------------------------------
    // screen -r (attach)
    // ------------------------------------------------------------------------
    else if (sub == "-r") {
        lock_guard<mutex> lock(queue_mutex);
        Process* p = find_process(rest);

        if (!p) {
            cout << "process not found\n";
            return;
        }

        cout << "Attached to " << p->name << "\n";
        string cmd;
        while (true) {
            cout << p->name << "> ";
            getline(cin, cmd);

            if (cmd == "process-smi") {
                cout << "PID: " << p->id << "\n";
                cout << "State: ";

                if (p->state == ProcessState::READY) cout << "READY\n";
                else if (p->state == ProcessState::RUNNING) cout << "RUNNING\n";
                else if (p->state == ProcessState::SLEEPING) cout << "SLEEPING\n";
                else if (p->state == ProcessState::FINISHED) cout << "FINISHED\n";
                else if (p->state == ProcessState::MEMORY_VIOLATED) cout << "MEMORY-VIOLATED\n";

                cout << "Instruction: " << p->current_instruction
                     << "/" << p->total_instructions << "\n";

                cout << "\nVariables:\n";
                for (auto& kv : p->memory)
                    cout << "  " << kv.first << " = " << kv.second << "\n";

                cout << "\nExecution log:\n";
                int shown = 0;
                for (auto it = p->exec_log.rbegin();
                     it != p->exec_log.rend() && shown < 10;
                     ++it, ++shown) {
                    cout << "  " << *it << "\n";
                }
            }
            else if (cmd == "exit") break;
        }
    }

    // ------------------------------------------------------------------------
    // screen -ls
    // ------------------------------------------------------------------------
    else if (sub == "-ls") {
        auto [used, avail, util] = calculate_cpu_utilization();
        cout << "CPU Utilization: " << fixed << setprecision(2) << util << "%\n";
        cout << "Processes:\n" << generate_process_list();
    }
}

// ============================================================================
// report-util
// ============================================================================

void handleReportUtil() {
    ofstream out("csopesy-log.txt");
    auto [used, avail, util] = calculate_cpu_utilization();

    out << "CPU Utilization: " << util << "%\n";
    out << generate_process_list();
    out.close();

    cout << "Report saved.\n";
}

// ============================================================================
// Command Dispatcher
// ============================================================================

void handleCommand(const string& cmd, const string& rest, bool& running) {
    if (!isInitialized && cmd != "initialize" && cmd != "exit" && cmd != "help") {
        cout << "Emulator not initialized.\n";
        return;
    }

    if (cmd == "exit") running = false;
    else if (cmd == "help") showHelp();
    else if (cmd == "initialize") {
        ifstream cfg("config.txt");
        if (!cfg.is_open()) {
            cout << "config.txt not found\n";
            return;
        }

        initializeConfig(cfg);
        cfg.close();

        if (!isValidConfig(config)) {
            cout << "Invalid config\n";
            return;
        }

        MemoryManager::getInstance().initialize();

        isInitialized = true;
        cpu_cores.resize(config.numCPU);
        srand(static_cast<unsigned>(time(nullptr)));
        start_scheduler_thread();

        cout << "Initialized.\n";
    }
    else if (cmd == "screen") handleScreenCommand(rest);
    else if (cmd == "scheduler-start") start_process_generation();
    else if (cmd == "scheduler-stop") stop_process_generation();
    else if (cmd == "report-util") handleReportUtil();
    else cout << "Unknown command\n";
}

// ============================================================================
// main()
// ============================================================================

int main() {
    showGreeting();

    bool running = true;
    string input;

    while (running) {
        cout << "> ";
        getline(cin, input);
        if (input.empty()) continue;

        auto [cmd, rest] = parseCommand(input);
        handleCommand(cmd, rest, running);
    }

    return 0;
}
