// Core_Config.h - SYSTEM-WIDE CONFIGURATION CONSTANTS
// COMPILER PROCESSES THIRD - Depends on Platform and Types
// Every constant here defines the limits and behavior of our ENTIRE system

#ifndef AARENDOCOREGLM_CORE_CONFIG_H
#define AARENDOCOREGLM_CORE_CONFIG_H

#include "Core_Platform.h"  // Foundation
#include "Core_Types.h"     // Type definitions

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// SYSTEM CAPACITY LIMITS - The boundaries of our universe
// ============================================================================

// Session limits - Our 10M concurrent sessions goal
constexpr u64 MAX_CONCURRENT_SESSIONS = 10'000'000;  // 10 million
constexpr u64 MAX_SESSION_ID = UINT64_MAX;           // Maximum possible session ID
constexpr u32 SESSIONS_PER_NUMA_NODE = 2'500'000;    // 2.5M per NUMA node (4 nodes)
constexpr u32 SESSION_POOL_SIZE = 1024;              // Pre-allocated sessions per pool

// Thread limits
constexpr u32 MAX_WORKER_THREADS = 256;              // Maximum worker threads
constexpr u32 DEFAULT_WORKER_THREADS = 16;           // Default thread count
constexpr u32 THREADS_PER_NUMA_NODE = 64;            // Threads per NUMA node

// Memory pool limits
constexpr usize MEMORY_POOL_SIZE = 16 * GB;          // THE memory pool size (PSYCHOTIC PRECISION)
constexpr usize DEFAULT_MEMORY_POOL_SIZE = 16 * GB;  // 16GB default pool
constexpr usize MIN_MEMORY_POOL_SIZE = 1 * GB;       // 1GB minimum
constexpr usize MAX_MEMORY_POOL_SIZE = 256 * GB;     // 256GB maximum
constexpr usize MEMORY_POOL_CHUNK_SIZE = 64 * MB;    // 64MB chunks

// ============================================================================
// PERFORMANCE CONSTANTS - Timing and latency targets
// ============================================================================

// Latency targets (in nanoseconds)
constexpr u64 TARGET_TICK_LATENCY_NS = 100;          // 100ns tick processing
constexpr u64 TARGET_ORDER_LATENCY_NS = 1000;        // 1µs order processing
constexpr u64 MAX_ACCEPTABLE_LATENCY_NS = 10000;     // 10µs max latency

// Timeout values
constexpr u64 DEFAULT_TIMEOUT_NS = 1'000'000;        // 1ms default timeout
constexpr u64 SPINLOCK_ITERATIONS = 1000;            // Spin iterations before yield
constexpr u64 BACKOFF_INITIAL_NS = 10;               // Initial backoff time
constexpr u64 BACKOFF_MAX_NS = 1000;                 // Max backoff time

// ============================================================================
// DATA STRUCTURE SIZES - Fixed sizes for our data structures
// ============================================================================

// String and vector sizes (no heap allocation)
constexpr usize FIXED_STRING_SIZE = 256;             // Fixed string capacity
constexpr usize FIXED_VECTOR_SIZE = 1024;            // Fixed vector capacity
constexpr usize MAX_SYMBOL_LENGTH = 32;              // Trading symbol max length
constexpr usize MAX_EXCHANGE_LENGTH = 16;            // Exchange name max length
constexpr usize MAX_ACCOUNT_ID_LENGTH = 64;          // Account ID max length (PSYCHOTIC PRECISION)
constexpr usize MAX_STRATEGY_NAME_LENGTH = 128;      // Strategy name max length (PSYCHOTIC PRECISION)

// Buffer sizes
constexpr usize TICK_BUFFER_SIZE = 65536;            // 64K ticks buffer
constexpr usize ORDER_BUFFER_SIZE = 4096;            // 4K orders buffer
constexpr usize MESSAGE_BUFFER_SIZE = 8192;          // 8K message buffer

// Queue sizes (power of 2 for efficiency)
constexpr usize TICK_QUEUE_SIZE = 1 << 20;           // 1M entries
constexpr usize ORDER_QUEUE_SIZE = 1 << 16;          // 64K entries
constexpr usize EVENT_QUEUE_SIZE = 1 << 18;          // 256K entries

// ============================================================================
// NINJATRADER INTEGRATION CONSTANTS
// ============================================================================

// NinjaTrader limits
constexpr u32 MAX_NINJATRADER_INSTRUMENTS = 1000;    // Max instruments
constexpr u32 MAX_NINJATRADER_STRATEGIES = 100;      // Max strategies
constexpr u32 NINJATRADER_TICK_BATCH_SIZE = 1000;    // Ticks per batch

// Market data constants
constexpr u32 MAX_MARKET_DEPTH_LEVELS = 10;          // DOM levels
constexpr u32 MAX_TIME_SALES_ENTRIES = 10000;        // Time & sales buffer

// ============================================================================
// SIMD AND OPTIMIZATION CONSTANTS
// ============================================================================

// SIMD alignment requirements
constexpr usize SIMD_ALIGNMENT = 32;                 // AVX2 requires 32-byte alignment
constexpr usize SIMD_REGISTER_SIZE = 256;            // 256-bit AVX2 registers
constexpr usize SIMD_DOUBLES_PER_REGISTER = 4;       // 4 doubles per AVX2 register
constexpr usize SIMD_FLOATS_PER_REGISTER = 8;        // 8 floats per AVX2 register

// Prefetch distances
constexpr usize PREFETCH_DISTANCE = 8;               // Cache lines to prefetch ahead
constexpr usize PREFETCH_STRIDE = CACHE_LINE;        // Prefetch stride

// ============================================================================
// LOGGING AND METRICS CONSTANTS
// ============================================================================

// Log levels
constexpr u32 LOG_LEVEL_NONE = 0;
constexpr u32 LOG_LEVEL_ERROR = 1;
constexpr u32 LOG_LEVEL_WARNING = 2;
constexpr u32 LOG_LEVEL_INFO = 3;
constexpr u32 LOG_LEVEL_DEBUG = 4;
constexpr u32 LOG_LEVEL_TRACE = 5;

// Metrics collection
constexpr u64 METRICS_SAMPLE_RATE = 1000;            // Sample every 1000 events
constexpr usize METRICS_BUFFER_SIZE = 1 << 16;       // 64K metrics entries
constexpr u32 METRICS_FLUSH_INTERVAL_MS = 1000;      // Flush every second

// ============================================================================
// ERROR HANDLING CONSTANTS
// ============================================================================

// Error buffer sizes
constexpr usize ERROR_MESSAGE_SIZE = 256;            // Error message max length
constexpr usize ERROR_STACK_SIZE = 1024;             // Error stack entries
constexpr u32 MAX_ERROR_RETRIES = 3;                 // Max retry attempts

// ============================================================================
// PERSISTENCE CONSTANTS
// ============================================================================

// Database limits
constexpr usize MAX_DB_CONNECTIONS = 32;             // Max DB connections
constexpr usize DB_BATCH_SIZE = 10000;               // Records per batch
constexpr u32 DB_FLUSH_INTERVAL_MS = 100;            // Flush interval

// File I/O
constexpr usize FILE_BUFFER_SIZE = 1 * MB;           // 1MB file buffer
constexpr u32 MAX_OPEN_FILES = 256;                  // Max open files

// ============================================================================
// COMPILATION FLAGS - Control feature compilation
// ============================================================================

// Feature flags
#define AARENDOCORE_ENABLE_METRICS 1                 // Enable metrics collection
#define AARENDOCORE_ENABLE_LOGGING 1                 // Enable logging
#define AARENDOCORE_ENABLE_PROFILING 1               // Enable profiling
#define AARENDOCORE_ENABLE_VALIDATION 1              // Enable validation checks

// Optimization flags
#define AARENDOCORE_USE_SIMD 1                       // Use SIMD optimizations
#define AARENDOCORE_USE_PREFETCH 1                   // Use prefetch instructions
#define AARENDOCORE_USE_LIKELY_UNLIKELY 1            // Use branch prediction hints

// ============================================================================
// STATIC VALIDATIONS - Ensure configuration is sane
// ============================================================================

// Validate power of 2 sizes
static_assert(IsPowerOfTwo<TICK_QUEUE_SIZE>, "Tick queue size must be power of 2");
static_assert(IsPowerOfTwo<ORDER_QUEUE_SIZE>, "Order queue size must be power of 2");
static_assert(IsPowerOfTwo<EVENT_QUEUE_SIZE>, "Event queue size must be power of 2");

// Validate alignment
static_assert(SIMD_ALIGNMENT >= 32, "SIMD alignment must be at least 32 bytes");
static_assert(SIMD_ALIGNMENT <= CACHE_LINE, "SIMD alignment shouldn't exceed cache line");

// Validate session limits
static_assert(MAX_CONCURRENT_SESSIONS <= MAX_SESSION_ID, "Session limit exceeds ID space");
static_assert(SESSIONS_PER_NUMA_NODE * 4 == MAX_CONCURRENT_SESSIONS, "NUMA distribution mismatch");

// Validate memory sizes
static_assert(DEFAULT_MEMORY_POOL_SIZE >= MIN_MEMORY_POOL_SIZE, "Default pool too small");
static_assert(DEFAULT_MEMORY_POOL_SIZE <= MAX_MEMORY_POOL_SIZE, "Default pool too large");

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCOREGLM_CORE_CONFIG_H