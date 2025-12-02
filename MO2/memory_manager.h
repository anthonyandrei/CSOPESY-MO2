/**
 * @file memory_manager.h
 * @brief Paging-based memory manager with FIFO/LRU replacement policies
 * 
 * Implements demand paging with configurable replacement algorithms.
 * Tracks page faults, manages backing store, and provides memory statistics.
 */

#pragma once
#include "config.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include <atomic>
#include <fstream>

extern Config config;

/**
 * @class MemoryManager
 * @brief Singleton memory manager for process paging
 * 
 * Features:
 * - Demand paging with page fault handling
 * - FIFO or LRU replacement policy (configured via config.replacementPolicy)
 * - Per-process page tables mapping virtual pages to physical frames
 * - Backing store simulation (csopesy-backing-store.txt)
 * - Memory statistics (RSS, paged in/out counts)
 */
class MemoryManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to the global MemoryManager instance
     */
    static MemoryManager& getInstance() {
        static MemoryManager instance;
        return instance;
    }

    /**
     * @brief Initialize memory manager and backing store
     * 
     * Allocates frame pool based on config.maxOverallMem / config.memPerFrame.
     * Resets backing store file.
     */
    void initialize();
    
    /**
     * @brief Allocate virtual memory for a process
     * @param pid Process ID
     * @param size Memory size in bytes
     * @return true (always succeeds - uses demand paging)
     * 
     * Creates page table entries initialized to -1 (not in RAM).
     * Actual frames are allocated on-demand via page faults.
     */
    bool allocateMemory(int pid, size_t size);
    
    /**
     * @brief Deallocate all memory for a process
     * @param pid Process ID
     * 
     * Frees all frames owned by this process and removes page table.
     */
    void deallocateMemory(int pid);
    
    /**
     * @brief Check if a virtual address is resident in physical memory
     * @param pid Process ID
     * @param virtualAddress Virtual address to check
     * @return true if page is in RAM, false if page fault needed
     * 
     * Side effect: Updates lastAccessedTick for LRU replacement policy.
     */
    bool isPageResident(int pid, uint32_t virtualAddress);
    
    /**
     * @brief Handle page fault by loading page into memory
     * @param pid Process ID
     * @param virtualAddress Virtual address that triggered fault
     * 
     * If no free frames available, evicts a victim using configured policy.
     */
    void requestPage(int pid, uint32_t virtualAddress);

    // Memory statistics
    size_t getFreeMemory();      ///< Get free memory in bytes
    size_t getUsedMemory();      ///< Get used memory in bytes
    size_t getTotalMemory();     ///< Get total physical memory in bytes
    size_t getProcessRSS(int pid); ///< Get resident set size (bytes) for process
    
    uint64_t getNumPagedIn();    ///< Total pages loaded from backing store
    uint64_t getNumPagedOut();   ///< Total pages evicted to backing store

private:
    MemoryManager() = default;
    
    /**
     * @struct Frame
     * @brief Represents one physical memory frame
     */
    struct Frame {
        int frameId;                 ///< Frame index in physical memory
        int ownerPid;                ///< Process that owns this frame (-1 if free)
        int pageNum;                 ///< Virtual page number mapped to this frame
        bool dirty;                  ///< True if frame modified (unused currently)
        uint64_t allocatedTick;      ///< CPU tick when frame was allocated (for FIFO)
        uint64_t lastAccessedTick;   ///< CPU tick when frame was last accessed (for LRU)
    };
    
    std::vector<Frame> frames;    ///< Physical frame pool
    size_t totalFrames = 0;       ///< Total number of frames

    /**
     * @brief Page tables: pageTables[pid][pageNum] = frameIndex
     * 
     * Maps virtual page numbers to physical frame indices.
     * Value -1 means page is not resident (in backing store).
     */
    std::unordered_map<int, std::unordered_map<int, int>> pageTables;

    std::atomic<uint64_t> pagedInCount{0};   ///< Total page-in operations
    std::atomic<uint64_t> pagedOutCount{0};  ///< Total page-out operations

    std::mutex memMutex;          ///< Protects all memory structures

    /**
     * @brief Convert virtual address to page number
     * @param addr Virtual address
     * @return Page number (addr / memPerFrame)
     */
    int getPageFromAddress(uint32_t addr);
    
    /**
     * @brief Find first free frame
     * @return Frame index or -1 if all frames occupied
     */
    int findFreeFrame();
    
    /**
     * @brief Select victim frame for eviction
     * @return Frame index to evict
     * 
     * Uses FIFO (smallest allocatedTick) or LRU (smallest lastAccessedTick)
     * based on config.replacementPolicy.
     */
    int selectVictimFrame();
    
    /**
     * @brief Evict a frame to backing store
     * @param frameIndex Frame to evict
     * 
     * Updates page table to mark page as not resident.
     * Logs swap-out to csopesy-backing-store.txt.
     */
    void swapOut(int frameIndex);
    
    /**
     * @brief Load a page into a frame
     * @param pid Process ID
     * @param pageNum Page number to load
     * @param frameIndex Destination frame
     * 
     * Sets allocatedTick and lastAccessedTick to current global_cpu_tick.
     * Updates page table mapping. Logs swap-in to backing store file.
     */
    void swapIn(int pid, int pageNum, int frameIndex);
};