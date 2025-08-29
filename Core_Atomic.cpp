// Core_Atomic.cpp - ATOMIC OPERATIONS IMPLEMENTATION
// Validates atomic operations and provides atomic utilities

#include "Core_Atomic.h"
#include <cstdio>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// ATOMIC VALIDATION
// ============================================================================

namespace {
    // Validate atomic operations at startup
    struct AtomicValidator {
        AtomicValidator() {
            ValidateLockFree();
            ValidateAtomicOperations();
            ValidateSpinlock();
            ValidateSequenceCounter();
        }
        
    private:
        void ValidateLockFree() {
            // Verify all our atomic types are truly lock-free
            AARENDOCORE_ASSERT(std::atomic<u8>{}.is_lock_free());
            AARENDOCORE_ASSERT(std::atomic<u16>{}.is_lock_free());
            AARENDOCORE_ASSERT(std::atomic<u32>{}.is_lock_free());
            AARENDOCORE_ASSERT(std::atomic<u64>{}.is_lock_free());
            AARENDOCORE_ASSERT(std::atomic<void*>{}.is_lock_free());
            
            // Verify atomic flag is lock-free
            std::atomic_flag flag = ATOMIC_FLAG_INIT;
            (void)flag; // Silence unused warning
        }
        
        void ValidateAtomicOperations() {
            // Test atomic increment/decrement
            std::atomic<u64> counter{0};
            
            auto val1 = AtomicIncrement(counter);
            AARENDOCORE_ASSERT(val1 == 0);  // Returns old value
            AARENDOCORE_ASSERT(counter.load() == 1);
            
            auto val2 = AtomicDecrement(counter);
            AARENDOCORE_ASSERT(val2 == 1);  // Returns old value
            AARENDOCORE_ASSERT(counter.load() == 0);
            
            // Test atomic add
            auto val3 = AtomicAdd(counter, u64(100));
            AARENDOCORE_ASSERT(val3 == 0);  // Returns old value
            AARENDOCORE_ASSERT(counter.load() == 100);
            
            // Test compare-exchange
            u64 expected = 100;
            bool success = AtomicCompareExchange(counter, expected, u64(200));
            AARENDOCORE_ASSERT(success);
            AARENDOCORE_ASSERT(counter.load() == 200);
            
            expected = 100;  // Wrong expected value
            success = AtomicCompareExchange(counter, expected, u64(300));
            AARENDOCORE_ASSERT(!success);
            AARENDOCORE_ASSERT(expected == 200);  // Updated to actual value
            AARENDOCORE_ASSERT(counter.load() == 200);  // Unchanged
            
            // Test exchange
            auto old = AtomicExchange(counter, u64(999));
            AARENDOCORE_ASSERT(old == 200);
            AARENDOCORE_ASSERT(counter.load() == 999);
        }
        
        void ValidateSpinlock() {
            Spinlock lock;
            
            // Test basic lock/unlock
            AARENDOCORE_ASSERT(!lock.is_locked());
            
            lock.lock();
            AARENDOCORE_ASSERT(lock.is_locked());
            
            // Try lock should fail when locked
            AARENDOCORE_ASSERT(!lock.try_lock());
            
            lock.unlock();
            AARENDOCORE_ASSERT(!lock.is_locked());
            
            // Try lock should succeed when unlocked
            AARENDOCORE_ASSERT(lock.try_lock());
            AARENDOCORE_ASSERT(lock.is_locked());
            
            lock.unlock();
            AARENDOCORE_ASSERT(!lock.is_locked());
        }
        
        void ValidateSequenceCounter() {
            SequenceCounter<u64> seq(1000);
            
            // Test initial value
            AARENDOCORE_ASSERT(seq.current() == 1000);
            
            // Test next
            auto val1 = seq.next();
            AARENDOCORE_ASSERT(val1 == 1000);  // Returns old value
            AARENDOCORE_ASSERT(seq.current() == 1001);
            
            auto val2 = seq.next();
            AARENDOCORE_ASSERT(val2 == 1001);
            AARENDOCORE_ASSERT(seq.current() == 1002);
            
            // Test reset
            seq.reset(5000);
            AARENDOCORE_ASSERT(seq.current() == 5000);
            
            auto val3 = seq.next();
            AARENDOCORE_ASSERT(val3 == 5000);
            AARENDOCORE_ASSERT(seq.current() == 5001);
        }
    };
    
    // Run validation at startup
    [[maybe_unused]] static AtomicValidator validator;
}

// ============================================================================
// ATOMIC INFORMATION EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetAtomicInfo() {
    static char info[512];
    
    std::snprintf(info, sizeof(info),
        "Atomic Info: "
        "u64_lockfree=%d, ptr_lockfree=%d, "
        "spinlock_size=%zu, sequence_size=%zu, "
        "cache_line=%zu",
        std::atomic<u64>{}.is_lock_free() ? 1 : 0,
        std::atomic<void*>{}.is_lock_free() ? 1 : 0,
        sizeof(Spinlock),
        sizeof(SequenceCounter<u64>),
        CACHE_LINE
    );
    
    return info;
}

// ============================================================================
// SPINLOCK PERFORMANCE TEST
// ============================================================================

extern "C" AARENDOCORE_API u64 AARendoCore_TestSpinlockPerformance(u32 iterations) {
    if (iterations == 0) {
        iterations = 1000000;  // Default 1M iterations
    }
    
    Spinlock lock;
    
    auto start = Clock::now();
    
    for (u32 i = 0; i < iterations; ++i) {
        lock.lock();
        // Critical section (empty for pure lock/unlock test)
        lock.unlock();
    }
    
    auto end = Clock::now();
    auto duration = std::chrono::duration_cast<Nanoseconds>(end - start);
    
    return static_cast<u64>(duration.count());
}

// ============================================================================
// SEQUENCE COUNTER PERFORMANCE TEST
// ============================================================================

extern "C" AARENDOCORE_API u64 AARendoCore_TestSequencePerformance(u32 iterations) {
    if (iterations == 0) {
        iterations = 10000000;  // Default 10M iterations
    }
    
    SequenceCounter<u64> counter;
    
    auto start = Clock::now();
    
    for (u32 i = 0; i < iterations; ++i) {
        [[maybe_unused]] auto val = counter.next();
    }
    
    auto end = Clock::now();
    auto duration = std::chrono::duration_cast<Nanoseconds>(end - start);
    
    return static_cast<u64>(duration.count());
}

// ============================================================================
// ATOMIC OPERATION EXPORTS
// ============================================================================

// Global sequence counter for session IDs (moved from Core_Types)
static SequenceCounter<u64> g_globalSequence(1);

extern "C" AARENDOCORE_API u64 AARendoCore_GetNextSequence() {
    return g_globalSequence.next();
}

extern "C" AARENDOCORE_API u64 AARendoCore_GetCurrentSequence() {
    return g_globalSequence.current();
}

extern "C" AARENDOCORE_API void AARendoCore_ResetSequence(u64 value) {
    g_globalSequence.reset(value);
}

AARENDOCORE_NAMESPACE_END