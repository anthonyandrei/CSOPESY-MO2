/**
 * @file config.h
 * @brief Configuration structure for CSOPESY OS Emulator
 * 
 * Defines the system-wide configuration parameters read from config.txt.
 * See CSOPESY - MO1 specs.txt pages 4-5 for parameter ranges and constraints.
 */

#pragma once
#include <string>
#include <cstdint>

/**
 * @struct Config
 * @brief System configuration parameters
 * 
 * All fields are initialized from config.txt during the 'initialize' command.
 * Valid ranges per specs:
 * - numCPU: [1, 128]
 * - scheduler: "fcfs" or "rr"
 * - quantumCycles: [1, 2^32]
 * - batchProcessFreq: [1, 2^32]
 * - minIns: [1, 2^32]
 * - maxIns: [minIns, 2^32]
 * - delaysPerExec: [0, 2^32]
 */
struct Config {
    int numCPU = 0;                     ///< Number of CPU cores (1-128)
    std::string scheduler;              ///< Scheduling algorithm: "fcfs" or "rr"
    uint32_t quantumCycles = 0;         ///< Time slice for Round Robin (CPU ticks)
    uint32_t batchProcessFreq = 0;      ///< Process generation frequency (CPU ticks)
    uint32_t minIns = 0;                ///< Minimum instructions per process
    uint32_t maxIns = 0;                ///< Maximum instructions per process
    uint32_t delaysPerExec = 0;         ///< Busy-wait delay per instruction (CPU ticks)
};
