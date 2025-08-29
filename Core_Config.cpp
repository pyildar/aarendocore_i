// Core_Config.cpp - CONFIGURATION VALIDATION AND RUNTIME CHECKS
// Ensures our configuration is valid and provides configuration info

#include "Core_Config.h"
#include <cstdio>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// CONFIGURATION VALIDATION
// ============================================================================

namespace {
    // Validate configuration at program startup
    struct ConfigValidator {
        ConfigValidator() {
            // Validate session configuration
            ValidateSessions();
            
            // Validate memory configuration
            ValidateMemory();
            
            // Validate performance targets
            ValidatePerformance();
            
            // Validate queue sizes
            ValidateQueues();
        }
        
    private:
        void ValidateSessions() {
            // Ensure we can actually handle 10M sessions
            constexpr u64 sessionMemory = MAX_CONCURRENT_SESSIONS * sizeof(SessionId);
            static_assert(sessionMemory < MAX_MEMORY_POOL_SIZE, 
                "Session IDs alone would exceed max memory pool");
            
            // Verify NUMA distribution
            AARENDOCORE_ASSERT(SESSIONS_PER_NUMA_NODE > 0);
            AARENDOCORE_ASSERT(SESSION_POOL_SIZE > 0);
            AARENDOCORE_ASSERT(SESSION_POOL_SIZE < SESSIONS_PER_NUMA_NODE);
        }
        
        void ValidateMemory() {
            // Check memory pool constraints
            AARENDOCORE_ASSERT(MIN_MEMORY_POOL_SIZE >= 1 * GB);
            AARENDOCORE_ASSERT(MAX_MEMORY_POOL_SIZE <= 1024 * GB);  // 1TB max
            AARENDOCORE_ASSERT(MEMORY_POOL_CHUNK_SIZE > 0);
            AARENDOCORE_ASSERT(MEMORY_POOL_CHUNK_SIZE <= DEFAULT_MEMORY_POOL_SIZE);
            
            // Verify chunk alignment
            AARENDOCORE_ASSERT((MEMORY_POOL_CHUNK_SIZE % PAGE_SIZE) == 0);
        }
        
        void ValidatePerformance() {
            // Sanity check latency targets
            AARENDOCORE_ASSERT(TARGET_TICK_LATENCY_NS < TARGET_ORDER_LATENCY_NS);
            AARENDOCORE_ASSERT(TARGET_ORDER_LATENCY_NS < MAX_ACCEPTABLE_LATENCY_NS);
            
            // Validate timeout values
            AARENDOCORE_ASSERT(DEFAULT_TIMEOUT_NS > MAX_ACCEPTABLE_LATENCY_NS);
            AARENDOCORE_ASSERT(BACKOFF_INITIAL_NS < BACKOFF_MAX_NS);
        }
        
        void ValidateQueues() {
            // All queue sizes must be non-zero and power of 2
            AARENDOCORE_ASSERT(TICK_QUEUE_SIZE > 0);
            AARENDOCORE_ASSERT(ORDER_QUEUE_SIZE > 0);
            AARENDOCORE_ASSERT(EVENT_QUEUE_SIZE > 0);
            
            // Verify they're powers of 2 at runtime
            AARENDOCORE_ASSERT((TICK_QUEUE_SIZE & (TICK_QUEUE_SIZE - 1)) == 0);
            AARENDOCORE_ASSERT((ORDER_QUEUE_SIZE & (ORDER_QUEUE_SIZE - 1)) == 0);
            AARENDOCORE_ASSERT((EVENT_QUEUE_SIZE & (EVENT_QUEUE_SIZE - 1)) == 0);
        }
    };
    
    // Run validation at startup
    [[maybe_unused]] static ConfigValidator validator;
}

// ============================================================================
// CONFIGURATION EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetConfigInfo() {
    static char info[1024];
    
    std::snprintf(info, sizeof(info),
        "Configuration: "
        "MaxSessions=%llu, SessionsPerNuma=%u, "
        "WorkerThreads=%u, MemoryPool=%lluGB, "
        "TargetLatency=%lluns, "
        "TickQueue=%zu, OrderQueue=%zu",
        static_cast<unsigned long long>(MAX_CONCURRENT_SESSIONS),
        SESSIONS_PER_NUMA_NODE,
        DEFAULT_WORKER_THREADS,
        static_cast<unsigned long long>(DEFAULT_MEMORY_POOL_SIZE / GB),
        static_cast<unsigned long long>(TARGET_TICK_LATENCY_NS),
        TICK_QUEUE_SIZE,
        ORDER_QUEUE_SIZE
    );
    
    return info;
}

extern "C" AARENDOCORE_API bool AARendoCore_ValidateConfig() {
    // Runtime configuration validation
    bool valid = true;
    
    // Check system resources
    valid &= (MAX_CONCURRENT_SESSIONS > 0);
    valid &= (DEFAULT_MEMORY_POOL_SIZE >= MIN_MEMORY_POOL_SIZE);
    valid &= (DEFAULT_WORKER_THREADS > 0 && DEFAULT_WORKER_THREADS <= MAX_WORKER_THREADS);
    
    // Check queue sizes are powers of 2
    valid &= ((TICK_QUEUE_SIZE & (TICK_QUEUE_SIZE - 1)) == 0);
    valid &= ((ORDER_QUEUE_SIZE & (ORDER_QUEUE_SIZE - 1)) == 0);
    
    return valid;
}

extern "C" AARENDOCORE_API u64 AARendoCore_GetMaxSessions() {
    return MAX_CONCURRENT_SESSIONS;
}

extern "C" AARENDOCORE_API u64 AARendoCore_GetMemoryPoolSize() {
    return DEFAULT_MEMORY_POOL_SIZE;
}

extern "C" AARENDOCORE_API u32 AARendoCore_GetWorkerThreads() {
    return DEFAULT_WORKER_THREADS;
}

AARENDOCORE_NAMESPACE_END