#include "memory_manager.h"
#include <iostream>
#include <algorithm>
#include <climits>
#include <atomic>
#include "scheduler.h"

extern std::atomic<uint64_t> global_cpu_tick;

void MemoryManager::initialize() {
    std::lock_guard<std::mutex> lock(memMutex);
    
    if (config.memPerFrame == 0) return;

    totalFrames = config.maxOverallMem / config.memPerFrame;
    frames.resize(totalFrames);

    for (int i = 0; i < totalFrames; ++i) {
        frames[i] = { i, -1, -1, false, 0 };
    }

    std::ofstream store("csopesy-backing-store.txt", std::ios::trunc);
    store.close();
}

bool MemoryManager::allocateMemory(int pid, size_t size) {
    std::lock_guard<std::mutex> lock(memMutex);
    

    size_t numPages = (size + config.memPerFrame - 1) / config.memPerFrame;
    
    //initialize entries to -1 meaning not in RAM/ invalid
    for (size_t i = 0; i < numPages; ++i) {
        pageTables[pid][i] = -1; 
    }

    return true;
}

void MemoryManager::deallocateMemory(int pid) {
    std::lock_guard<std::mutex> lock(memMutex);

    for (auto& frame : frames) {
        if (frame.ownerPid == pid) {
            frame.ownerPid = -1;
            frame.pageNum = -1;
            frame.dirty = false;
        }
    }
    
    pageTables.erase(pid);
}

int MemoryManager::getPageFromAddress(uint32_t addr) {
    return addr / config.memPerFrame;
}

bool MemoryManager::isPageResident(int pid, uint32_t virtualAddress) {
    std::lock_guard<std::mutex> lock(memMutex);
    
    int pageNum = getPageFromAddress(virtualAddress);
    
    if (pageTables.find(pid) == pageTables.end()) return false;
    if (pageTables[pid].find(pageNum) == pageTables[pid].end()) return false;

    int frameIndex = pageTables[pid][pageNum];
    return (frameIndex != -1);
}

void MemoryManager::requestPage(int pid, uint32_t virtualAddress) {
    std::lock_guard<std::mutex> lock(memMutex);
    
    int pageNum = getPageFromAddress(virtualAddress);
    

    if (pageTables[pid][pageNum] != -1) return;


    int frameIndex = findFreeFrame();

    if (frameIndex == -1) {
        frameIndex = selectVictimFrame();
        swapOut(frameIndex);
    }

    swapIn(pid, pageNum, frameIndex);
}

int MemoryManager::findFreeFrame() {
    for (int i = 0; i < totalFrames; ++i) {
        if (frames[i].ownerPid == -1) return i;
    }
    return -1;
}

int MemoryManager::selectVictimFrame() {

    int oldestFrame = -1;
    uint64_t minTick = UINT64_MAX;

    for (int i = 0; i < totalFrames; ++i) {
        if (frames[i].allocatedTick < minTick) {
            minTick = frames[i].allocatedTick;
            oldestFrame = i;
        }
    }
    return oldestFrame;
}

void MemoryManager::swapOut(int frameIndex) {
    Frame& f = frames[frameIndex];
    
    if (f.ownerPid != -1) {

        std::ofstream store("csopesy-backing-store.txt", std::ios::app);
        store << "SwapOut: PID " << f.ownerPid << " Page " << f.pageNum 
              << " from Frame " << f.frameId << "\n";
        store.close();


        pageTables[f.ownerPid][f.pageNum] = -1;//no longer in RAM
        pagedOutCount++;
    }
}

void MemoryManager::swapIn(int pid, int pageNum, int frameIndex) {

    std::ofstream store("csopesy-backing-store.txt", std::ios::app);
    store << "SwapIn: PID " << pid << " Page " << pageNum 
          << " into Frame " << frameIndex << "\n";
    store.close();

    frames[frameIndex].ownerPid = pid;
    frames[frameIndex].pageNum = pageNum;
    frames[frameIndex].dirty = false;

    frames[frameIndex].allocatedTick = 0;


    pageTables[pid][pageNum] = frameIndex;
    pagedInCount++;
}

size_t MemoryManager::getTotalMemory() {
    return config.maxOverallMem;
}

size_t MemoryManager::getUsedMemory() {
    std::lock_guard<std::mutex> lock(memMutex);
    size_t used = 0;
    for(const auto& f : frames) {
        if(f.ownerPid != -1) used++;
    }
    return used * config.memPerFrame;
}

size_t MemoryManager::getFreeMemory() {
    return getTotalMemory() - getUsedMemory();
}

size_t MemoryManager::getProcessRSS(int pid) {
    std::lock_guard<std::mutex> lock(memMutex);
    size_t pages = 0;
    for(const auto& f : frames) {
        if(f.ownerPid == pid) pages++;
    }
    return pages * config.memPerFrame;
}

uint64_t MemoryManager::getNumPagedIn() { return pagedInCount.load(); }
uint64_t MemoryManager::getNumPagedOut() { return pagedOutCount.load(); }