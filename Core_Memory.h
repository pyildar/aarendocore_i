// Core_Memory.h - MEMORY MANAGEMENT BASE
// COMPILER PROCESSES SIXTH - After atomic operations
// Base memory management with extreme precision
// Every allocation tracked, every byte accounted for

#ifndef AARENDOCOREGLM_CORE_MEMORY_H
#define AARENDOCOREGLM_CORE_MEMORY_H

#include "Core_Platform.h"   // Foundation
#include "Core_Types.h"      // Type system
#include "Core_Config.h"     // System constants
#include "Core_Alignment.h"  // Alignment utilities
#include "Core_Atomic.h"     // Atomic operations

#include <cstring>           // For memcpy, memset
#include <new>               // For placement new

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// MEMORY STATISTICS - Track every allocation
// ============================================================================

struct MemoryStats {
    AtomicU64 totalAllocated{0};      // Total bytes allocated
    AtomicU64 totalFreed{0};          // Total bytes freed
    AtomicU64 currentUsage{0};        // Current bytes in use
    AtomicU64 peakUsage{0};           // Peak bytes used
    AtomicU64 allocationCount{0};     // Number of allocations
    AtomicU64 freeCount{0};           // Number of frees
    AtomicU64 failedAllocations{0};   // Failed allocation attempts
};

// Global memory statistics
extern MemoryStats g_memoryStats;

// ============================================================================
// MEMORY POOL - Pre-allocated memory for zero-allocation operations
// ============================================================================

class MemoryPool {
private:
    byte* memory_;                     // Pool memory
    usize size_;                       // Total pool size
    AtomicU64 offset_;                 // Current allocation offset
    Spinlock lock_;                    // For thread safety
    u32 alignment_;                    // Default alignment
    
public:
    MemoryPool() noexcept;
    explicit MemoryPool(usize size, u32 alignment = CACHE_LINE) noexcept;
    ~MemoryPool() noexcept;
    
    // Disable copy
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    // Enable move
    MemoryPool(MemoryPool&& other) noexcept;
    MemoryPool& operator=(MemoryPool&& other) noexcept;
    
    // Initialize pool
    bool initialize(usize size, u32 alignment = CACHE_LINE) noexcept;
    
    // Allocate from pool
    void* allocate(usize size, u32 alignment = 0) noexcept;
    
    // Reset pool (frees all allocations)
    void reset() noexcept;
    
    // Get pool info
    usize capacity() const noexcept { return size_; }
    usize used() const noexcept { return offset_.load(MemoryOrderRelaxed); }
    usize available() const noexcept { return size_ - used(); }
    bool is_initialized() const noexcept { return memory_ != nullptr; }
    
    // Release pool memory
    void release() noexcept;
};

// ============================================================================
// STACK ALLOCATOR - Ultra-fast stack-based allocation
// ============================================================================

template<usize SIZE>
class StackAllocator {
private:
    alignas(CACHE_LINE) byte buffer_[SIZE];
    usize offset_;
    
public:
    StackAllocator() noexcept : offset_(0) {}
    
    // Allocate from stack
    void* allocate(usize size, u32 alignment = alignof(std::max_align_t)) noexcept {
        // Align offset
        offset_ = AlignUp(offset_, alignment);
        
        // Check if enough space
        if (offset_ + size > SIZE) {
            return nullptr;
        }
        
        void* ptr = &buffer_[offset_];
        offset_ += size;
        
        return ptr;
    }
    
    // Reset stack
    void reset() noexcept {
        offset_ = 0;
    }
    
    // Get info
    constexpr usize capacity() const noexcept { return SIZE; }
    usize used() const noexcept { return offset_; }
    usize available() const noexcept { return SIZE - offset_; }
};

// ============================================================================
// MEMORY OPERATIONS - Optimized memory manipulation
// ============================================================================

// Zero memory - optimized for different sizes
AARENDOCORE_FORCEINLINE void MemoryZero(void* ptr, usize size) noexcept {
    std::memset(ptr, 0, size);
}

// Copy memory - optimized for different sizes
AARENDOCORE_FORCEINLINE void MemoryCopy(void* dest, const void* src, usize size) noexcept {
    std::memcpy(dest, src, size);
}

// Move memory - handles overlapping regions
AARENDOCORE_FORCEINLINE void MemoryMove(void* dest, const void* src, usize size) noexcept {
    std::memmove(dest, src, size);
}

// Compare memory
AARENDOCORE_FORCEINLINE i32 MemoryCompare(const void* a, const void* b, usize size) noexcept {
    return std::memcmp(a, b, size);
}

// Fill memory with value
AARENDOCORE_FORCEINLINE void MemoryFill(void* ptr, byte value, usize size) noexcept {
    std::memset(ptr, value, size);
}

// ============================================================================
// ALIGNED ALLOCATION - For cache and SIMD alignment
// ============================================================================

// Allocate aligned memory
void* AllocateAligned(usize size, usize alignment) noexcept;

// Free aligned memory
void FreeAligned(void* ptr) noexcept;

// Reallocate aligned memory
void* ReallocateAligned(void* ptr, usize old_size, usize new_size, usize alignment) noexcept;

// ============================================================================
// OBJECT CONSTRUCTION - Placement new wrappers
// ============================================================================

// Construct object in-place
template<typename T, typename... Args>
AARENDOCORE_FORCEINLINE T* Construct(void* ptr, Args&&... args) noexcept {
    return new(ptr) T(std::forward<Args>(args)...);
}

// Destroy object (call destructor)
template<typename T>
AARENDOCORE_FORCEINLINE void Destroy(T* ptr) noexcept {
    if (ptr) {
        ptr->~T();
    }
}

// Construct array of objects
template<typename T>
AARENDOCORE_FORCEINLINE void ConstructArray(T* ptr, usize count) noexcept {
    for (usize i = 0; i < count; ++i) {
        new(&ptr[i]) T();
    }
}

// Destroy array of objects
template<typename T>
AARENDOCORE_FORCEINLINE void DestroyArray(T* ptr, usize count) noexcept {
    for (usize i = 0; i < count; ++i) {
        ptr[i].~T();
    }
}

// ============================================================================
// MEMORY GUARD - RAII memory management
// ============================================================================

class MemoryGuard {
private:
    void* ptr_;
    bool owned_;
    
public:
    explicit MemoryGuard(void* ptr) noexcept : ptr_(ptr), owned_(true) {}
    
    ~MemoryGuard() noexcept {
        if (owned_ && ptr_) {
            FreeAligned(ptr_);
        }
    }
    
    // Disable copy
    MemoryGuard(const MemoryGuard&) = delete;
    MemoryGuard& operator=(const MemoryGuard&) = delete;
    
    // Enable move
    MemoryGuard(MemoryGuard&& other) noexcept 
        : ptr_(other.ptr_), owned_(other.owned_) {
        other.owned_ = false;
    }
    
    MemoryGuard& operator=(MemoryGuard&& other) noexcept {
        if (this != &other) {
            if (owned_ && ptr_) {
                FreeAligned(ptr_);
            }
            ptr_ = other.ptr_;
            owned_ = other.owned_;
            other.owned_ = false;
        }
        return *this;
    }
    
    // Release ownership
    void* release() noexcept {
        owned_ = false;
        return ptr_;
    }
    
    // Get pointer
    void* get() const noexcept { return ptr_; }
    
    // Check if valid
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
};

// ============================================================================
// MEMORY UTILITIES
// ============================================================================

// Get system page size
usize GetSystemPageSize() noexcept;

// Get total system memory
usize GetTotalSystemMemory() noexcept;

// Get available system memory
usize GetAvailableSystemMemory() noexcept;

// Check if pointer is valid
bool IsValidPointer(const void* ptr) noexcept;

// ============================================================================
// COMPILE-TIME MEMORY CALCULATIONS
// ============================================================================

// Calculate total size for array
template<typename T>
constexpr usize ArraySize(usize count) noexcept {
    return sizeof(T) * count;
}

// Calculate aligned size
constexpr usize AlignedSize(usize size, usize alignment) noexcept {
    return (size + alignment - 1) & ~(alignment - 1);
}

// ============================================================================
// STATIC ASSERTIONS
// ============================================================================

// Verify memory pool fits in cache line multiple
static_assert(sizeof(MemoryPool) % CACHE_LINE == 0, 
    "MemoryPool should be cache line aligned size");

// Verify stack allocator alignment
static_assert(alignof(StackAllocator<1024>) >= CACHE_LINE,
    "StackAllocator should be cache line aligned");

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCOREGLM_CORE_MEMORY_H