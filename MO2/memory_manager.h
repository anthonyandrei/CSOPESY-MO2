#pragma once
#include "config.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include <atomic>
#include <fstream>

extern Config config;

class MemoryManager {
public:
    static MemoryManager& getInstance() {
        static MemoryManager instance;
        return instance;
    }

    void initialize();
    bool allocateMemory(int pid, size_t size);
    void deallocateMemory(int pid);
    bool isPageResident(int pid, uint32_t virtualAddress);
    void requestPage(int pid, uint32_t virtualAddress);

    
    size_t getFreeMemory();
    size_t getUsedMemory();
    size_t getTotalMemory();
    
    
    size_t getProcessRSS(int pid);
    
    uint64_t getNumPagedIn();
    uint64_t getNumPagedOut();

private:
    MemoryManager() = default;
    
    struct Frame {
        int frameId;
        int ownerPid;
        int pageNum;
        bool dirty;
        uint64_t allocatedTick; 
    };
    std::vector<Frame> frames;
    size_t totalFrames = 0;

    std::unordered_map<int, std::unordered_map<int, int>> pageTables;

    std::atomic<uint64_t> pagedInCount{0};
    std::atomic<uint64_t> pagedOutCount{0};

    std::mutex memMutex;

    int getPageFromAddress(uint32_t addr);
    int findFreeFrame();
    int selectVictimFrame();
    void swapOut(int frameIndex);
    void swapIn(int pid, int pageNum, int frameIndex);
};