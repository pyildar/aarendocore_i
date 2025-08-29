// Core_Memory.cpp - MEMORY MANAGEMENT IMPLEMENTATION
// Provides memory allocation, tracking, and utilities

#include "Core_Memory.h"
#include <cstdio>
#include <cstdlib>

#if AARENDOCORE_PLATFORM_WINDOWS
    #include <windows.h>
    #include <malloc.h>
#else
    #include <unistd.h>
    #include <sys/mman.h>
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// GLOBAL MEMORY STATISTICS
// ============================================================================

MemoryStats g_memoryStats;

// ============================================================================
// MEMORY POOL IMPLEMENTATION
// ============================================================================

MemoryPool::MemoryPool() noexcept 
    : memory_(nullptr), size_(0), offset_(0), alignment_(CACHE_LINE) {
}

MemoryPool::MemoryPool(usize size, u32 alignment) noexcept
    : memory_(nullptr), size_(0), offset_(0), alignment_(alignment) {
    initialize(size, alignment);
}

MemoryPool::~MemoryPool() noexcept {
    release();
}

MemoryPool::MemoryPool(MemoryPool&& other) noexcept
    : memory_(other.memory_), 
      size_(other.size_),
      offset_(other.offset_.load()),
      alignment_(other.alignment_) {
    other.memory_ = nullptr;
    other.size_ = 0;
    other.offset_.store(0);
}

MemoryPool& MemoryPool::operator=(MemoryPool&& other) noexcept {
    if (this != &other) {
        release();
        
        memory_ = other.memory_;
        size_ = other.size_;
        offset_.store(other.offset_.load());
        alignment_ = other.alignment_;
        
        other.memory_ = nullptr;
        other.size_ = 0;
        other.offset_.store(0);
    }
    return *this;
}

bool MemoryPool::initialize(usize size, u32 alignment) noexcept {
    if (memory_) {
        return false;  // Already initialized
    }
    
    if (size == 0 || alignment == 0) {
        return false;
    }
    
    // Ensure alignment is power of 2
    if ((alignment & (alignment - 1)) != 0) {
        return false;
    }
    
    // Allocate aligned memory
    memory_ = static_cast<byte*>(AllocateAligned(size, alignment));
    if (!memory_) {
        AtomicIncrement(g_memoryStats.failedAllocations);
        return false;
    }
    
    size_ = size;
    alignment_ = alignment;
    offset_.store(0);
    
    // Zero the memory
    MemoryZero(memory_, size_);
    
    // Update stats
    AtomicAdd(g_memoryStats.totalAllocated, size);
    AtomicAdd(g_memoryStats.currentUsage, size);
    AtomicIncrement(g_memoryStats.allocationCount);
    
    // Update peak usage
    u64 current = g_memoryStats.currentUsage.load(MemoryOrderRelaxed);
    u64 peak = g_memoryStats.peakUsage.load(MemoryOrderRelaxed);
    while (current > peak) {
        if (g_memoryStats.peakUsage.compare_exchange_weak(peak, current,
            MemoryOrderRelaxed, MemoryOrderRelaxed)) {
            break;
        }
    }
    
    return true;
}

void* MemoryPool::allocate(usize size, u32 alignment) noexcept {
    if (!memory_ || size == 0) {
        return nullptr;
    }
    
    // Use default alignment if not specified
    if (alignment == 0) {
        alignment = alignment_;
    }
    
    // Ensure alignment is power of 2
    if ((alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
    
    // Lock for thread safety
    lock_.lock();
    
    // Calculate aligned offset
    usize current_offset = offset_.load(MemoryOrderRelaxed);
    usize aligned_offset = AlignUp(current_offset, alignment);
    
    // Check if enough space
    if (aligned_offset + size > size_) {
        lock_.unlock();
        AtomicIncrement(g_memoryStats.failedAllocations);
        return nullptr;
    }
    
    // Update offset
    offset_.store(aligned_offset + size, MemoryOrderRelaxed);
    
    lock_.unlock();
    
    return memory_ + aligned_offset;
}

void MemoryPool::reset() noexcept {
    lock_.lock();
    offset_.store(0, MemoryOrderRelaxed);
    lock_.unlock();
}

void MemoryPool::release() noexcept {
    if (memory_) {
        lock_.lock();
        
        // Update stats
        AtomicAdd(g_memoryStats.totalFreed, size_);
        // Subtract from current usage - but AtomicU64 can't be negative, so we use fetch_sub directly
        g_memoryStats.currentUsage.fetch_sub(size_, MemoryOrderRelaxed);
        AtomicIncrement(g_memoryStats.freeCount);
        
        FreeAligned(memory_);
        memory_ = nullptr;
        size_ = 0;
        offset_.store(0);
        
        lock_.unlock();
    }
}

// ============================================================================
// ALIGNED ALLOCATION IMPLEMENTATION
// ============================================================================

void* AllocateAligned(usize size, usize alignment) noexcept {
    if (size == 0 || alignment == 0) {
        return nullptr;
    }
    
    // Ensure alignment is power of 2
    if ((alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
    
    void* ptr = nullptr;
    
#if AARENDOCORE_PLATFORM_WINDOWS
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#endif
    
    if (ptr) {
        // Update stats
        AtomicAdd(g_memoryStats.totalAllocated, size);
        AtomicAdd(g_memoryStats.currentUsage, size);
        AtomicIncrement(g_memoryStats.allocationCount);
        
        // Update peak usage
        u64 current = g_memoryStats.currentUsage.load(MemoryOrderRelaxed);
        u64 peak = g_memoryStats.peakUsage.load(MemoryOrderRelaxed);
        while (current > peak) {
            if (g_memoryStats.peakUsage.compare_exchange_weak(peak, current,
                MemoryOrderRelaxed, MemoryOrderRelaxed)) {
                break;
            }
        }
    } else {
        AtomicIncrement(g_memoryStats.failedAllocations);
    }
    
    return ptr;
}

void FreeAligned(void* ptr) noexcept {
    if (!ptr) {
        return;
    }
    
#if AARENDOCORE_PLATFORM_WINDOWS
    _aligned_free(ptr);
#else
    free(ptr);
#endif
    
    // Note: We can't track the size being freed without additional bookkeeping
    AtomicIncrement(g_memoryStats.freeCount);
}

void* ReallocateAligned(void* ptr, usize old_size, usize new_size, usize alignment) noexcept {
    if (new_size == 0) {
        FreeAligned(ptr);
        return nullptr;
    }
    
    if (!ptr) {
        return AllocateAligned(new_size, alignment);
    }
    
    // Allocate new block
    void* new_ptr = AllocateAligned(new_size, alignment);
    if (!new_ptr) {
        return nullptr;
    }
    
    // Copy old data
    usize copy_size = (old_size < new_size) ? old_size : new_size;
    MemoryCopy(new_ptr, ptr, copy_size);
    
    // Free old block
    FreeAligned(ptr);
    
    return new_ptr;
}

// ============================================================================
// SYSTEM MEMORY INFORMATION
// ============================================================================

usize GetSystemPageSize() noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return static_cast<usize>(sysconf(_SC_PAGESIZE));
#endif
}

usize GetTotalSystemMemory() noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return static_cast<usize>(ms.ullTotalPhys);
    }
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        return static_cast<usize>(pages) * static_cast<usize>(page_size);
    }
    return 0;
#endif
}

usize GetAvailableSystemMemory() noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return static_cast<usize>(ms.ullAvailPhys);
    }
    return 0;
#else
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        return static_cast<usize>(pages) * static_cast<usize>(page_size);
    }
    return 0;
#endif
}

bool IsValidPointer(const void* ptr) noexcept {
    if (!ptr) {
        return false;
    }
    
#if AARENDOCORE_PLATFORM_WINDOWS
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi))) {
        return (mbi.State == MEM_COMMIT) && 
               (mbi.Protect != PAGE_NOACCESS) &&
               (mbi.Protect != PAGE_GUARD);
    }
    return false;
#else
    // Simple check - try to read from the address
    // This is not perfect but works for basic validation
    volatile const char* test = static_cast<const char*>(ptr);
    [[maybe_unused]] volatile char value = *test;
    return true;  // If we didn't crash, it's valid
#endif
}

// ============================================================================
// MEMORY INFORMATION EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetMemoryInfo() {
    static char info[512];
    
    std::snprintf(info, sizeof(info),
        "Memory: Allocated=%llu, Freed=%llu, Current=%llu, Peak=%llu, "
        "Allocations=%llu, Frees=%llu, Failed=%llu",
        static_cast<unsigned long long>(g_memoryStats.totalAllocated.load()),
        static_cast<unsigned long long>(g_memoryStats.totalFreed.load()),
        static_cast<unsigned long long>(g_memoryStats.currentUsage.load()),
        static_cast<unsigned long long>(g_memoryStats.peakUsage.load()),
        static_cast<unsigned long long>(g_memoryStats.allocationCount.load()),
        static_cast<unsigned long long>(g_memoryStats.freeCount.load()),
        static_cast<unsigned long long>(g_memoryStats.failedAllocations.load())
    );
    
    return info;
}

extern "C" AARENDOCORE_API u64 AARendoCore_GetMemoryUsage() {
    return g_memoryStats.currentUsage.load(MemoryOrderRelaxed);
}

extern "C" AARENDOCORE_API u64 AARendoCore_GetPeakMemoryUsage() {
    return g_memoryStats.peakUsage.load(MemoryOrderRelaxed);
}

AARENDOCORE_NAMESPACE_END