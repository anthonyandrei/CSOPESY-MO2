/**
 * @file main.cpp
 * @brief Main CLI shell and command interpreter for CSOPESY OS Emulator
 * 
 * Implements command-line interface, configuration loading, and command routing.
 * Responsible for user interaction and dispatching commands to scheduler.
 * 
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

using namespace std;

// ============================================================================
// Global state
// ============================================================================
Config config;                  ///< System configuration (loaded from config.txt)
bool isInitialized = false;     ///< Initialization guard (prevents commands before setup)
bool verboseMode = true;        ///< Enable debug output

// ============================================================================
// Helper functions
// ============================================================================

/**
 * @brief Find process by name in all queues (except finished)
 * @param name Process name to search for
 * @return Pointer to process if found, nullptr otherwise
 */
Process* find_process(const std::string& name) {
    // Search ready queue
    for (auto& proc : ready_queue) {
        if (proc.name == name) return &proc;
    }
    
    // Search sleeping queue
    for (auto& proc : sleeping_queue) {
        if (proc.name == name) return &proc;
    }
    
    // Search CPU cores
    for (auto& proc : cpu_cores) {
        if (proc.has_value() && proc.value().name == name) {
            return &proc.value();
        }
    }
    
    return nullptr;
}

/**
 * @brief Calculate CPU utilization metrics
 * @return Tuple of (cores_used, cores_available, utilization_percentage)
 */
std::tuple<int, int, double> calculate_cpu_utilization() {
    int coresUsed = 0;
    for (auto& core : cpu_cores) {
        if (core.has_value()) coresUsed++;
    }
    int coresAvailable = cpu_cores.size() - coresUsed;
    double cpuUtilization = (cpu_cores.size() > 0) 
        ? (static_cast<double>(coresUsed) / cpu_cores.size()) * 100.0 
        : 0.0;
    
    return std::make_tuple(coresUsed, coresAvailable, cpuUtilization);
}

/**
 * @brief Generate formatted process list string
 * @return String containing all processes with their states
 */
std::string generate_process_list() {
    std::stringstream ss;
    
    for (auto& p : ready_queue) 
        ss << p.name << " [READY]\n";
    for (auto& core : cpu_cores) {
        if (core.has_value())
            ss << core.value().name << " [RUNNING]\n";
    }
    for (auto& p : sleeping_queue) 
        ss << p.name << " [SLEEPING]\n";
    for (auto& p : finished_queue) 
        ss << p.name << " [FINISHED]\n";
    
    return ss.str();
}

/**
 * @brief Display startup greeting banner
 * 
 * Shows emulator title, team member names, and initial instructions.
 */
void showGreeting() {
    cout << "=====================================\n";
    cout << "          CSOPESY OS Emulator        \n";
    cout << "=====================================\n";
    cout << "Welcome to CSOPESY Emulator!\n";
    cout << "Developers:\n";
    cout << "Alonzo, John Leomarc\n";
    cout << "Labarrete, Lance\n";
    cout << "Soan, Brent Jan\n";
    cout << "Tan, Anthony Andrei C.\n";
    cout << "Last updated: November 3, 2025\n";
    cout << "-------------------------------------\n";
    cout << "Type 'initialize' to start, or 'help' for commands.\n\n";
}

/**
 * @brief Display available commands and usage
 * 
 * Shows list of all supported commands with brief descriptions.
 */
void showHelp() {
    cout << "\nAvailable Commands:\n";
    cout << "==================\n";
    cout << "initialize            - Load config.txt and start scheduler\n";
    cout << "screen -s <name>      - Create new process with given name\n";
    cout << "screen -r <name>      - Attach to process console\n";
    cout << "screen -ls            - List all processes and their states\n";
    cout << "scheduler-start       - Begin periodic process generation\n";
    cout << "scheduler-stop        - Stop periodic process generation\n";
    cout << "report-util           - Display CPU utilization report\n";
    cout << "help                  - Show this help message\n";
    cout << "exit                  - Exit emulator\n";
    cout << "\nInside process screen:\n";
    cout << "  process-smi         - Show process info and variables\n";
    cout << "  exit                - Return to main menu\n\n";
}

/**
 * @brief Trim leading whitespace from string (in-place)
 * @param s String to modify
 * 
 * Removes all leading spaces, tabs, newlines, etc.
 */
void trimLeadingSpaces(string& s) {
    size_t i = 0;
    while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i) s.erase(0, i);
}

/**
 * @brief Parse user input into command and parameter
 * @param input Raw input line from user
 * @return Pair of (command, parameter)
 * 
 * Example:
 *   "screen -s process1" -> ("screen", "-s process1")
 *   "initialize" -> ("initialize", "")
 */
pair<string, string> parseCommand(const string& input) {
    stringstream ss(input);
    string command, param;
    ss >> command;
    getline(ss, param);
    trimLeadingSpaces(param);
    return make_pair(command, param);
}

/**
 * @brief Read and parse config.txt into global Config struct
 * @param configFile Open file stream to config.txt
 * 
 * Expected format (one key-value pair per line):
 *   num-cpu <value>
 *   scheduler <fcfs|rr>
 *   quantum-cycles <value>
 *   batch-process-freq <value>
 *   min-ins <value>
 *   max-ins <value>
 *   delays-per-exec <value>
 * 
 * Unknown keys are skipped with a warning.
 * See specs pg. 4-5 for parameter ranges.
 */
void initializeConfig(ifstream& configFile) {
    string key;

    while (configFile >> key) {
        if (key == "num-cpu") {
            configFile >> config.numCPU;
            // Resize CPU core array (thread-safe via mutex)
            std::lock_guard<std::mutex> lock(queue_mutex);
            cpu_cores.resize(config.numCPU);
        } 
        else if (key == "scheduler") {
            configFile >> config.scheduler;
        } 
        else if (key == "quantum-cycles") {
            configFile >> config.quantumCycles;
        } 
        else if (key == "batch-process-freq") {
            configFile >> config.batchProcessFreq;
        } 
        else if (key == "min-ins") {
            configFile >> config.minIns;
        } 
        else if (key == "max-ins") {
            configFile >> config.maxIns;
        } 
        else if (key == "delays-per-exec") {
            configFile >> config.delaysPerExec;
        } 
        else {
            // Skip unknown keys (consume value token and continue)
            string skip;
            configFile >> skip;
            cout << "WARNING: Unknown key '" << key << "', skipping value '" << skip << "'" << endl;
        }
    }
}

/**
 * @brief Validate configuration parameters against specs
 * @param config Configuration to validate
 * @return true if all parameters are within valid ranges
 * 
 * Validation rules (specs pg. 4-5):
 * - numCPU: [1, 128]
 * - scheduler: "fcfs" or "rr"
 * - quantumCycles: >= 1
 * - batchProcessFreq: >= 1
 * - minIns: >= 1
 * - maxIns: >= minIns
 * - delaysPerExec: [0, 2^32] (automatically valid for uint32_t)
 */
bool isValidConfig(const Config& config) {
    if (config.numCPU < 1 || config.numCPU > 128)
        return false;
    if (config.scheduler != "fcfs" && config.scheduler != "rr")
        return false;
    if (config.quantumCycles < 1u)
        return false;
    if (config.batchProcessFreq < 1u)
        return false;
    if (config.minIns < 1u || config.maxIns < config.minIns)
        return false;
    return true;
}

// ============================================================================
// Command handlers
// ============================================================================

/**
 * @brief Handle screen command with subcommands
 * @param param The parameter string (e.g., "-s name", "-r name", "-ls")
 * 
 * Subcommands:
 * - screen -s <name>: Create new process with hardcoded test instructions
 * - screen -r <name>: Attach to process console (mini REPL)
 * - screen -ls: List all processes with CPU utilization
 */
void handleScreenCommand(const string& param) {
    auto [subcommand, subparam] = parseCommand(param);

    // screen -s <name>: Create new process manually
    if (subcommand == "-s") {
        // Clear console contents per specs pg. 3
        system("cls");
        
        Process newP(next_process_id++, subparam, 10);
        newP.instructions = {
            //{"DECLARE", {"x","10"}},
            //{"DECLARE", {"y","5"}},
            //{"DECLARE", {"counter","0"}},
            //{"FOR", {"3", "3"}},              // Loop 3 times over next 3 instructions
            //{"ADD", {"x","x","y"}},           // x = x + y
            //{"ADD", {"counter","counter","1"}},  // counter++
            //{"PRINT", {"Iteration: +counter, x = +x"}},
            //{"SUBTRACT", {"x","x","3"}},      // After loop: x = x - 3
            //{"PRINT", {"Final: x = +x, counter = +counter"}},
            //{"PRINT", {}}                      // Default message: "Hello world from [process]!"

            // Test case 5
            { "DECLARE", { "x", "0" } },
            { "FOR", { std::to_string(config.minIns), "2" } },
            { "PRINT", { "Value from: +x" } },
            { "ADD", { "x", "x", std::to_string(rand() % 10 + 1) } },
        };

        std::lock_guard<std::mutex> lock(queue_mutex);
        ready_queue.push_back(std::move(newP));
        std::cout << "New process " << subparam << " created.\n";
    }
    // screen -r <name>: Attach to process console
    else if (subcommand == "-r") {
        std::lock_guard<std::mutex> lock(queue_mutex);
        Process* targetProc = find_process(subparam);

        if (targetProc) {
            // Clear console contents per specs pg. 3
            system("cls");
            
            std::cout << "Attached to " << subparam << std::endl;

            // Mini REPL inside process screen
            std::string cmd;
            while (true) {
                std::cout << subparam << "> ";
                getline(std::cin, cmd);

                if (cmd == "process-smi") {
                    // Display process state and variables (per specs pg. 3)
                    std::cout << "Process: " << targetProc->name << "\n";
                    std::cout << "Current instruction line: " << targetProc->current_instruction << "\n";
                    std::cout << "Total lines of code: " << targetProc->total_instructions << "\n";
                    
                    // Print "Finished!" if process completed (specs pg. 3)
                    if (targetProc->state == ProcessState::FINISHED) {
                        std::cout << "Finished!\n";
                    }
                    
                    std::cout << "\nVariables:\n";
                    for (auto& kv : targetProc->memory)
                        std::cout << "  " << kv.first << " = " << kv.second << "\n";
                }
                else if (cmd == "exit") {
                    std::cout << "Returning to main menu...\n";
                    break;
                }
            }
        }
        else {
            std::cout << "Error: Process " << subparam << " not found.\n";
        }
    }
    // screen -ls: List all processes
    else if (subcommand == "-ls") {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        auto [coresUsed, coresAvailable, cpuUtilization] = calculate_cpu_utilization();
        
        std::cout << "CPU utilization: " << std::fixed << std::setprecision(2) 
                  << cpuUtilization << "%\n";
        std::cout << "Cores used: " << coresUsed << "\n";
        std::cout << "Cores available: " << coresAvailable << "\n\n";
        
        std::cout << "Processes:\n";
        std::cout << generate_process_list();
    }
}

/**
 * @brief Handle report-util command
 * 
 * Generates CPU utilization report and saves to csopesy-log.txt.
 * Includes cores used/available, utilization percentage, and process list.
 */
void handleReportUtil() {
    if (verboseMode) cout << "[DEBUG] Generating report..." << endl;

    std::lock_guard<std::mutex> lock(queue_mutex);
    std::ofstream log("csopesy-log.txt");

    auto [coresUsed, coresAvailable, cpuUtilization] = calculate_cpu_utilization();
    
    log << "CPU utilization: " << std::fixed << std::setprecision(2) 
        << cpuUtilization << "%\n";
    log << "Cores used: " << coresUsed << "\n";
    log << "Cores available: " << coresAvailable << "\n\n";
    
    log << "Processes:\n";
    log << generate_process_list();

    log.close();
    
    std::cout << "Report saved to csopesy-log.txt\n";
}

/**
 * @brief Process and execute user commands
 * @param command Primary command (e.g., "initialize", "screen")
 * @param param Command parameter (e.g., "-s process1")
 * @param isRunning Reference to main loop flag (set false to exit)
 * 
 * Enforces initialization gating: all commands except 'initialize' and 'exit'
 * are blocked until isInitialized is true.
 * 
 * Screen subcommands:
 * - screen -s <name>: Create new process with hardcoded test instructions
 * - screen -r <name>: Attach to process (searches ready/sleeping/running queues, not finished)
 * - screen -ls: List all processes with their states (ready/running/sleeping/finished)
 * 
 * See specs pg. 1-2 for command descriptions.
 */
void handleCommand(const string command, const string param, bool& isRunning) {
    // Initialization guard (specs requirement)
    if (!isInitialized && command != "initialize" && command != "exit" && command != "help") {
        cout << "Error: Emulator not initialized. Please run 'initialize' first." << endl;
        return;
    }
    
    if (command == "exit") {
        cout << "Exiting CSOPESY Emulator..." << endl;
        isRunning = false;
    }
    else if (command == "help") {
        showHelp();
    }
    else if (command == "initialize") {
        // Prevent double initialization
        if (isInitialized) {
            cout << "Emulator is already initialized." << endl;
            return;
        }

        // Load config.txt
        if (verboseMode) cout << "[DEBUG] Reading config.txt..." << endl;
        ifstream configFile("config.txt");
        if (!configFile.is_open()) {
            cout << "Error: config.txt not found!" << endl;
            return;
        }
        initializeConfig(configFile);
        configFile.close();

        // Validate configuration
        if (!isValidConfig(config)) {
            cout << "Error: Invalid configuration parameters!" << endl;
            return;
        }

        // Mark initialized and start background scheduler
        isInitialized = true;
        cout << "Configuration loaded successfully." << endl;
        
        // Seed RNG before starting scheduler thread
        srand(static_cast<unsigned int>(time(nullptr)));
        start_scheduler_thread();
    }
    else if (command == "screen") {
        handleScreenCommand(param);
    }
    else if (command == "scheduler-start") {
        if (verboseMode) cout << "[DEBUG] Starting scheduler..." << endl;
        start_process_generation();
        cout << "Process generation started." << endl;
    }
    else if (command == "scheduler-stop") {
        if (verboseMode) cout << "[DEBUG] Stopping scheduler..." << endl;
        stop_process_generation();
        cout << "Process generation stopped." << endl;
    }
    else if (command == "report-util") {
        handleReportUtil();
    }
    else {
        cout << "Unknown command: " << command << endl;
    }
}

// ============================================================================
// Main entry point
// ============================================================================

/**
 * @brief Main program loop
 * 
 * Displays greeting, then enters command loop:
 * 1. Display prompt ("> ")
 * 2. Read user input
 * 3. Parse command and parameter
 * 4. Execute command via handleCommand()
 * 5. Repeat until isRunning is false
 */
int main() {
    bool isRunning = true;
    string input;

    showGreeting();

    while (isRunning) {
        cout << "> ";
        getline(cin, input);

        if (input.empty()) 
            continue;

        auto [command, param] = parseCommand(input);
        handleCommand(command, param, isRunning);
    }

    return 0;
}
