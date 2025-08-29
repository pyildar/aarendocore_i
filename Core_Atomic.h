// Core_Atomic.h - ATOMIC OPERATIONS WRAPPER
// COMPILER PROCESSES FIFTH - After foundation layer
// Wraps std::atomic with our extreme performance requirements
// Every memory fence, every ordering is DELIBERATE

#ifndef AARENDOCOREGLM_CORE_ATOMIC_H
#define AARENDOCOREGLM_CORE_ATOMIC_H

#include "Core_Platform.h"   // Foundation - compiler settings
#include "Core_Types.h"      // Our type system including atomics
#include "Core_Config.h"     // System constants
#include "Core_Alignment.h"  // Cache line alignment

#include <atomic>            // std::atomic operations
#include <thread>            // std::this_thread::yield()

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// MEMORY ORDER DEFINITIONS - For ultra-precise control
// ============================================================================

// Use relaxed for counters that don't synchronize
constexpr auto MemoryOrderRelaxed = std::memory_order_relaxed;

// Use acquire for reading synchronization variables
constexpr auto MemoryOrderAcquire = std::memory_order_acquire;

// Use release for writing synchronization variables
constexpr auto MemoryOrderRelease = std::memory_order_release;

// Use acq_rel for read-modify-write operations
constexpr auto MemoryOrderAcqRel = std::memory_order_acq_rel;

// Use seq_cst only when absolutely necessary (expensive!)
constexpr auto MemoryOrderSeqCst = std::memory_order_seq_cst;

// ============================================================================
// ATOMIC OPERATIONS - Lock-free guarantees for 10M sessions
// ============================================================================

// Atomic increment with configurable memory order
template<typename T>
AARENDOCORE_FORCEINLINE T AtomicIncrement(std::atomic<T>& value, 
                                          std::memory_order order = MemoryOrderRelaxed) noexcept {
    return value.fetch_add(1, order);
}

// Atomic decrement with configurable memory order
template<typename T>
AARENDOCORE_FORCEINLINE T AtomicDecrement(std::atomic<T>& value,
                                          std::memory_order order = MemoryOrderRelaxed) noexcept {
    return value.fetch_sub(1, order);
}

// Atomic add with configurable memory order
template<typename T>
AARENDOCORE_FORCEINLINE T AtomicAdd(std::atomic<T>& value, T delta,
                                    std::memory_order order = MemoryOrderRelaxed) noexcept {
    return value.fetch_add(delta, order);
}

// Atomic compare and swap (CAS)
template<typename T>
AARENDOCORE_FORCEINLINE bool AtomicCompareExchange(std::atomic<T>& value,
                                                   T& expected,
                                                   T desired,
                                                   std::memory_order success = MemoryOrderAcqRel,
                                                   std::memory_order failure = MemoryOrderAcquire) noexcept {
    return value.compare_exchange_strong(expected, desired, success, failure);
}

// Atomic exchange
template<typename T>
AARENDOCORE_FORCEINLINE T AtomicExchange(std::atomic<T>& value, T desired,
                                         std::memory_order order = MemoryOrderAcqRel) noexcept {
    return value.exchange(desired, order);
}

// ============================================================================
// SPINLOCK - Ultra-fast lock for short critical sections
// ============================================================================

class CACHE_ALIGNED Spinlock {
private:
    std::atomic<bool> locked_{false};
    
public:
    Spinlock() noexcept = default;
    
    // Disable copy and move - locks can't be copied!
    Spinlock(const Spinlock&) = delete;
    Spinlock& operator=(const Spinlock&) = delete;
    Spinlock(Spinlock&&) = delete;
    Spinlock& operator=(Spinlock&&) = delete;
    
    // Try to acquire lock without spinning
    AARENDOCORE_FORCEINLINE bool try_lock() noexcept {
        // First check if it's locked (read-only, no write)
        if (locked_.load(MemoryOrderRelaxed)) {
            return false;
        }
        
        // Try to acquire
        bool expected = false;
        return locked_.compare_exchange_strong(expected, true, 
                                              MemoryOrderAcquire, 
                                              MemoryOrderRelaxed);
    }
    
    // Acquire lock with spinning
    AARENDOCORE_FORCEINLINE void lock() noexcept {
        // Spin with exponential backoff
        u32 spin_count = 0;
        
        while (!try_lock()) {
            // Spin a bit before yielding
            if (++spin_count < SPINLOCK_ITERATIONS) {
                // CPU pause instruction for hyper-threading
                #if AARENDOCORE_ARCH_X64
                    _mm_pause();
                #endif
            } else {
                // Yield to OS scheduler
                std::this_thread::yield();
                spin_count = 0;
            }
        }
    }
    
    // Release lock
    AARENDOCORE_FORCEINLINE void unlock() noexcept {
        locked_.store(false, MemoryOrderRelease);
    }
    
    // Check if locked (for debugging)
    AARENDOCORE_FORCEINLINE bool is_locked() const noexcept {
        return locked_.load(MemoryOrderRelaxed);
    }
};

// ============================================================================
// SEQUENCE COUNTER - For generating unique IDs at extreme speed
// ============================================================================

template<typename T = u64>
class CACHE_ALIGNED SequenceCounter {
private:
    std::atomic<T> counter_;
    
public:
    explicit SequenceCounter(T initial = 0) noexcept : counter_(initial) {}
    
    // Get next sequence number
    AARENDOCORE_FORCEINLINE T next() noexcept {
        return counter_.fetch_add(1, MemoryOrderRelaxed);
    }
    
    // Get current value without incrementing
    AARENDOCORE_FORCEINLINE T current() const noexcept {
        return counter_.load(MemoryOrderRelaxed);
    }
    
    // Reset to value
    AARENDOCORE_FORCEINLINE void reset(T value = 0) noexcept {
        counter_.store(value, MemoryOrderRelaxed);
    }
};

// ============================================================================
// ATOMIC FLAG - Single bit atomic with no false sharing
// ============================================================================

class CACHE_ALIGNED AtomicFlag {
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    
public:
    AtomicFlag() noexcept = default;
    
    // Set flag and return previous value
    AARENDOCORE_FORCEINLINE bool test_and_set(
        std::memory_order order = MemoryOrderAcqRel) noexcept {
        return flag_.test_and_set(order);
    }
    
    // Clear flag
    AARENDOCORE_FORCEINLINE void clear(
        std::memory_order order = MemoryOrderRelease) noexcept {
        flag_.clear(order);
    }
    
    // Test flag (C++20)
    #if __cplusplus >= 202002L
    AARENDOCORE_FORCEINLINE bool test(
        std::memory_order order = MemoryOrderAcquire) const noexcept {
        return flag_.test(order);
    }
    #endif
};

// ============================================================================
// MEMORY BARRIERS - Explicit fence operations
// ============================================================================

// Full memory barrier
AARENDOCORE_FORCEINLINE void MemoryBarrier() noexcept {
    std::atomic_thread_fence(MemoryOrderSeqCst);
}

// Acquire barrier (prevents reads from being reordered before)
AARENDOCORE_FORCEINLINE void AcquireBarrier() noexcept {
    std::atomic_thread_fence(MemoryOrderAcquire);
}

// Release barrier (prevents writes from being reordered after)
AARENDOCORE_FORCEINLINE void ReleaseBarrier() noexcept {
    std::atomic_thread_fence(MemoryOrderRelease);
}

// Compiler barrier (prevents compiler reordering only)
AARENDOCORE_FORCEINLINE void CompilerBarrier() noexcept {
    std::atomic_signal_fence(MemoryOrderSeqCst);
}

// ============================================================================
// ATOMIC POINTER OPERATIONS - For lock-free data structures
// ============================================================================

template<typename T>
class AtomicPtr {
private:
    std::atomic<T*> ptr_;
    
public:
    AtomicPtr(T* initial = nullptr) noexcept : ptr_(initial) {}
    
    // Load pointer
    AARENDOCORE_FORCEINLINE T* load(
        std::memory_order order = MemoryOrderAcquire) const noexcept {
        return ptr_.load(order);
    }
    
    // Store pointer
    AARENDOCORE_FORCEINLINE void store(T* value,
        std::memory_order order = MemoryOrderRelease) noexcept {
        ptr_.store(value, order);
    }
    
    // Exchange pointer
    AARENDOCORE_FORCEINLINE T* exchange(T* value,
        std::memory_order order = MemoryOrderAcqRel) noexcept {
        return ptr_.exchange(value, order);
    }
    
    // Compare and swap pointer
    AARENDOCORE_FORCEINLINE bool compare_exchange(T*& expected, T* desired,
        std::memory_order success = MemoryOrderAcqRel,
        std::memory_order failure = MemoryOrderAcquire) noexcept {
        return ptr_.compare_exchange_strong(expected, desired, success, failure);
    }
};

// ============================================================================
// PAUSE OPERATIONS - For spin-wait loops
// ============================================================================

// CPU pause instruction for better hyper-threading
AARENDOCORE_FORCEINLINE void CpuPause() noexcept {
#if AARENDOCORE_ARCH_X64
    _mm_pause();
#else
    // Fallback to compiler barrier
    CompilerBarrier();
#endif
}

// Yield to other threads
AARENDOCORE_FORCEINLINE void ThreadYield() noexcept {
    std::this_thread::yield();
}

// ============================================================================
// STATIC ASSERTIONS - Verify atomic properties
// ============================================================================

// Ensure our atomic types are lock-free
static_assert(std::atomic<u64>::is_always_lock_free, "u64 atomics must be lock-free");
static_assert(std::atomic<i64>::is_always_lock_free, "i64 atomics must be lock-free");
static_assert(std::atomic<void*>::is_always_lock_free, "pointer atomics must be lock-free");

// Ensure Spinlock fits in cache line
static_assert(sizeof(Spinlock) <= CACHE_LINE, "Spinlock must fit in cache line");

// Ensure SequenceCounter fits in cache line
static_assert(sizeof(SequenceCounter<u64>) <= CACHE_LINE, 
    "SequenceCounter must fit in cache line");

AARENDOCORE_NAMESPACE_END

// Include intrinsics for pause instruction
#if AARENDOCORE_ARCH_X64
    #include <emmintrin.h>  // For _mm_pause
#endif

#endif // AARENDOCOREGLM_CORE_ATOMIC_H