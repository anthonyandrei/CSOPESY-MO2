#include <iostream>
#include <string>
#include <sstream>
#include <utility>
#include <fstream>
#include <cstdint>

using namespace std;

struct Config {
    int numCPU = 0;
    string scheduler;
    uint32_t quantumCycles = 0;
    uint32_t batchProcessFreq = 0;
    uint32_t minIns = 0;
    uint32_t maxIns = 0;
    uint32_t delaysPerExec = 0;
};

Config config;
bool isInitialized = false;
bool verboseMode = true;

// Show startup greeting
void showGreeting() {
    cout << "=====================================\n";
    cout << "          CSOPESY OS Emulator        \n";
    cout << "=====================================\n";
    cout << "Welcome to CSOPESY Emulator!\n";
    cout << "Developers: [names]\n";
    cout << "Last updated: [date]\n";
    cout << "-------------------------------------\n";
    cout << "Type 'initialize' to start, or 'exit' to quit.\n\n";
}

// Trim leading spaces
void trimLeadingSpaces(string& s) {
    size_t i = 0;
    while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i) s.erase(0, i);
}

// Parse user input into command and parameter
pair<string, string> parseCommand(const string& input) {
    stringstream ss(input);
    string command, param;
    ss >> command;
    getline(ss, param);
    trimLeadingSpaces(param);
    return make_pair(command, param);
}

void initializeConfig(ifstream& configFile) {
    string key;

    while (configFile >> key) {
        if (key == "num-cpu") {
            configFile >> config.numCPU;
        } else if (key == "scheduler") {
            configFile >> config.scheduler;
        } else if (key == "quantum-cycles") {
            configFile >> config.quantumCycles;
        } else if (key == "batch-process-freq") {
            configFile >> config.batchProcessFreq;
        } else if (key == "min-ins") {
            configFile >> config.minIns;
        } else if (key == "max-ins") {
            configFile >> config.maxIns;
        } else if (key == "delays-per-exec") {
            configFile >> config.delaysPerExec;
        } else {
            // Skip one token (the value) and continue
            string skip;
            if (configFile >> skip) {
                cout << "WARNING: Unknown key '" << key
                     << "', skipping value '" << skip << "'" << endl;
                continue;
            }
            cout << "WARNING: Unknown key '" << key << "'" << endl;
            return;
        }
    }
}

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
    // delays-per-exec: [0, 2^32]; uint32_t already guards upper bound, only need non-negative
    return true;
}

// Handle recognized commands
void handleCommand(const string command, const string param, bool& isRunning) {
    if (!isInitialized && command != "initialize" && command != "exit") {
        cout << "Error: Emulator not initialized. Please run 'initialize' first." << endl;
        return;
    }
    
    
    if (command == "exit") {
        cout << "Exiting CSOPESY Emulator..." << endl;
        isRunning = false;
    } else if (command == "initialize") {
        if (isInitialized) {
            cout << "Emulator is already initialized." << endl;
            return;
        }

        cout << "[initialize] Reading config.txt..." << endl;
        ifstream configFile("config.txt");
        if (!configFile.is_open()) {
            cout << "Error: config.txt not found!" << endl;
            return;
        }
        initializeConfig(configFile);
        configFile.close();
        if (!isValidConfig(config)) {
            cout << "Error: Invalid configuration parameters!" << endl;
            return;
        }
        isInitialized = true;
        cout << "Configuration loaded successfully." << endl;
    }
    else if (command == "screen") {
        if (verboseMode)
            cout << "[DEBUG] Command "<< command <<" received with param: " << param << endl;
        // TODO: implement screen logic
        auto [subcommand, subparam] = parseCommand(param);
        if (subcommand == "-s") {
            if (verboseMode)
                cout << "[DEBUG] -s subcommand with param: " << subparam << endl;
        } else if (subcommand == "-r") {
            if (verboseMode)
                cout << "[DEBUG] -r subcommand with param: " << subparam << endl;
        } else if (subcommand == "-ls") {
            if (verboseMode)
                cout << "[DEBUG] -ls subcommand with param: " << subparam << endl;
        }
    }
    else if (command == "scheduler-start") {
        if (verboseMode)
            cout << "[DEBUG] Starting scheduler..." << endl;
        // TODO: link to scheduler module
    }
    else if (command == "scheduler-stop") {
        if (verboseMode)
            cout << "[DEBUG] Stopping scheduler..." << endl;
        // TODO: link to scheduler stop
    }
    else if (command == "report-util") {
        if (verboseMode)
            cout << "[DEBUG] Generating report..." << endl;
        // TODO: call report generator
    }
    else {
        cout << "Unknown command: " << command << endl;
    }
}

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
