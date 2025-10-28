#pragma once
#include <string>
#include <cstdint>

// System-wide configuration struct
struct Config {
    int numCPU = 0;
    std::string scheduler;
    uint32_t quantumCycles = 0;
    uint32_t batchProcessFreq = 0;
    uint32_t minIns = 0;
    uint32_t maxIns = 0;
    uint32_t delaysPerExec = 0;
};
