/**
 * @file memory_manager.cpp
 * @brief Implementation of paging-based memory management
 * 
 * Handles demand paging, page replacement (FIFO/LRU), and backing store simulation.
 * Integrates with scheduler via global_cpu_tick for timestamp-based replacement.
 */

#include "memory_manager.h"
#include <iostream>
#include <algorithm>
#include <climits>
#include <atomic>
#include "scheduler.h"

// Global CPU tick used for FIFO/LRU timestamp tracking
extern std::atomic<uint64_t> global_cpu_tick;

void MemoryManager::initialize() {
    std::lock_guard<std::mutex> lock(memMutex);
    
    // Guard against invalid config
    if (config.memPerFrame == 0) return;

    // Calculate total number of frames
    totalFrames = config.maxOverallMem / config.memPerFrame;
    frames.resize(totalFrames);

    // Initialize all frames as free (ownerPid = -1)
    for (int i = 0; i < totalFrames; ++i) {
        frames[i] = { i, -1, -1, false, 0, 0 };
    }

    // Reset backing store log file
    std::ofstream store("csopesy-backing-store.txt", std::ios::trunc);
    store.close();
}

bool MemoryManager::allocateMemory(int pid, size_t size) {
    std::lock_guard<std::mutex> lock(memMutex);
    
    // Calculate number of pages needed (round up)
    size_t numPages = (size + config.memPerFrame - 1) / config.memPerFrame;
    
    // Initialize page table entries to -1 (not resident in RAM)
    // Actual frames allocated on-demand when pages are accessed
    for (size_t i = 0; i < numPages; ++i) {
        pageTables[pid][i] = -1; 
    }

    return true; // Always succeeds (demand paging)
}

void MemoryManager::deallocateMemory(int pid) {
    std::lock_guard<std::mutex> lock(memMutex);

    // Free all frames owned by this process
    for (auto& frame : frames) {
        if (frame.ownerPid == pid) {
            frame.ownerPid = -1;  // Mark frame as free
            frame.pageNum = -1;
            frame.dirty = false;
        }
    }
    
    // Remove process page table
    pageTables.erase(pid);
}

int MemoryManager::getPageFromAddress(uint32_t addr) {
    if (config.memPerFrame == 0) return 0;  // Guard against invalid config
    return addr / config.memPerFrame;
}

bool MemoryManager::isPageResident(int pid, uint32_t virtualAddress) {
    std::lock_guard<std::mutex> lock(memMutex);
    
    int pageNum = getPageFromAddress(virtualAddress);
    
    if (pageTables.find(pid) == pageTables.end()) return false;
    if (pageTables[pid].find(pageNum) == pageTables[pid].end()) return false;

    int frameIndex = pageTables[pid][pageNum];
    if (frameIndex != -1) {
        // Update last accessed time for LRU policy
        frames[frameIndex].lastAccessedTick = global_cpu_tick.load();
        return true;
    }
    return false;
}

void MemoryManager::requestPage(int pid, uint32_t virtualAddress) {
    std::lock_guard<std::mutex> lock(memMutex);
    
    int pageNum = getPageFromAddress(virtualAddress);
    
    // If page is already resident, nothing to do
    if (pageTables[pid][pageNum] != -1) return;

    // Try to find a free frame first
    int frameIndex = findFreeFrame();

    // If no free frames, evict a victim using configured replacement policy
    if (frameIndex == -1) {
        frameIndex = selectVictimFrame();
        swapOut(frameIndex);
    }

    // Load the requested page into the frame
    swapIn(pid, pageNum, frameIndex);
}

int MemoryManager::findFreeFrame() {
    for (int i = 0; i < totalFrames; ++i) {
        if (frames[i].ownerPid == -1) return i;
    }
    return -1;
}

int MemoryManager::selectVictimFrame() {
    int victim = -1;
    uint64_t bestTick = UINT64_MAX;

    if (config.replacementPolicy == "lru") {
        // LRU (Least Recently Used): Evict the frame that hasn't been
        // accessed for the longest time (smallest lastAccessedTick)
        for (int i = 0; i < totalFrames; ++i) {
            if (frames[i].lastAccessedTick < bestTick) {
                bestTick = frames[i].lastAccessedTick;
                victim = i;
            }
        }
    } else {
        // FIFO (First In First Out): Evict the oldest frame
        // (smallest allocatedTick = earliest arrival time)
        for (int i = 0; i < totalFrames; ++i) {
            if (frames[i].allocatedTick < bestTick) {
                bestTick = frames[i].allocatedTick;
                victim = i;
            }
        }
    }
    return victim;
}

void MemoryManager::swapOut(int frameIndex) {
    Frame& f = frames[frameIndex];
    
    if (f.ownerPid != -1) {
        // Log eviction to backing store file
        std::ofstream store("csopesy-backing-store.txt", std::ios::app);
        store << "SwapOut: PID " << f.ownerPid << " Page " << f.pageNum 
              << " from Frame " << f.frameId << "\n";
        store.close();

        // Mark page as not resident in page table
        pageTables[f.ownerPid][f.pageNum] = -1;
        pagedOutCount++;
    }
}

void MemoryManager::swapIn(int pid, int pageNum, int frameIndex) {
    // Log page load to backing store file
    std::ofstream store("csopesy-backing-store.txt", std::ios::app);
    store << "SwapIn: PID " << pid << " Page " << pageNum 
          << " into Frame " << frameIndex << "\n";
    store.close();

    // Assign frame to this process and page
    frames[frameIndex].ownerPid = pid;
    frames[frameIndex].pageNum = pageNum;
    frames[frameIndex].dirty = false;

    // Set timestamps to current CPU tick (critical for FIFO/LRU)
    uint64_t now = global_cpu_tick.load();
    frames[frameIndex].allocatedTick = now;      // Used by FIFO
    frames[frameIndex].lastAccessedTick = now;   // Used by LRU

    // Update page table mapping
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