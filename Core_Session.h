// Core_Session.h - SESSION DATA STRUCTURE
// COMPILER PROCESSES AFTER MEMORY & THREADING
// Each session represents ONE active trading session
// Designed for 10M concurrent sessions with ZERO contention

#ifndef AARENDOCOREGLM_CORE_SESSION_H
#define AARENDOCOREGLM_CORE_SESSION_H

#include "Core_Platform.h"
#include "Core_Types.h"
#include "Core_Config.h"
#include "Core_Alignment.h"
#include "Core_Atomic.h"
#include "Core_Memory.h"
#include "Core_NUMA.h"      // PSYCHOTIC PRECISION: Need MAX_NUMA_NODES

#include <chrono>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// SESSION STATE ENUMERATION
// ============================================================================

enum class SessionState : u32 {
    Uninitialized = 0,
    Initializing  = 1,
    Active        = 2,
    Paused        = 3,
    Closing       = 4,
    Closed        = 5,
    Error         = 6
};

// ============================================================================
// SESSION FLAGS - Bit flags for session properties
// ============================================================================

enum SessionFlags : u32 {
    SESSION_FLAG_NONE           = 0x00000000,
    SESSION_FLAG_REALTIME       = 0x00000001,  // Real-time data processing
    SESSION_FLAG_HISTORICAL     = 0x00000002,  // Historical data processing
    SESSION_FLAG_PAPER_TRADING  = 0x00000004,  // Paper trading mode
    SESSION_FLAG_LIVE_TRADING   = 0x00000008,  // Live trading mode
    SESSION_FLAG_HIGH_PRIORITY  = 0x00000010,  // High priority session
    SESSION_FLAG_NUMA_AWARE     = 0x00000020,  // NUMA-aware allocation
    SESSION_FLAG_PERSISTENT     = 0x00000040,  // Persistent session data
    SESSION_FLAG_ENCRYPTED      = 0x00000080   // Encrypted session data
};

// ============================================================================
// SESSION STATISTICS - Per-session performance metrics
// ============================================================================

struct alignas(CACHE_LINE) SessionStatistics {
    // Timing metrics
    u64 creationTime;        // Nanoseconds since epoch
    u64 lastActivityTime;    // Last tick processed
    u64 totalProcessingTime; // Total nanoseconds processing
    
    // Event counters
    AtomicU64 ticksProcessed{0};
    AtomicU64 ordersSubmitted{0};
    AtomicU64 ordersExecuted{0};
    AtomicU64 ordersCancelled{0};
    
    // Performance metrics
    AtomicU64 messagesSent{0};
    AtomicU64 messagesReceived{0};
    AtomicU64 bytesTransferred{0};
    AtomicU64 errorCount{0};
    
    // Cache performance
    AtomicU64 cacheHits{0};
    AtomicU64 cacheMisses{0};
    
    SessionStatistics() noexcept;
    void reset() noexcept;
};

// ============================================================================
// SESSION CONFIGURATION - Immutable session settings
// ============================================================================

struct SessionConfiguration {
    // Identity
    char accountId[MAX_ACCOUNT_ID_LENGTH];
    char strategyName[MAX_STRATEGY_NAME_LENGTH];
    
    // Resource limits
    u32 maxOrdersPerSecond;
    u32 maxPositions;
    u32 maxPendingOrders;
    u64 maxMemoryUsage;
    
    // Performance settings
    u32 tickQueueSize;
    u32 orderQueueSize;
    u32 eventQueueSize;
    u32 numaNode;           // Preferred NUMA node
    
    // Flags
    u32 flags;
    
    SessionConfiguration() noexcept;
    void setDefaults() noexcept;
};

// ============================================================================
// SESSION DATA - Core session structure (CACHE ALIGNED)
// ============================================================================

struct alignas(ULTRA_PAGE) SessionData {
    // Session identity (immutable after creation)
    SessionId id;
    u32 sessionIndex;        // Index in session manager
    u32 numaNode;           // NUMA node affinity
    
    // Session state (atomic for lock-free access)
    std::atomic<SessionState> state{SessionState::Uninitialized};
    AtomicU32 flags{SESSION_FLAG_NONE};
    
    // Configuration (read-mostly)
    SessionConfiguration config;
    
    // Statistics (frequently updated)
    SessionStatistics stats;
    
    // Memory pool for session allocations
    MemoryPool* memoryPool;
    
    // Thread affinity
    u64 cpuAffinity;        // CPU mask for thread affinity
    u32 workerId;           // Assigned worker thread
    
    // Timestamps (nanoseconds)
    u64 createdAt;
    u64 lastTickAt;
    u64 lastHeartbeatAt;
    
    // User data pointer (for strategy-specific data)
    void* userData;
    
    // Constructor/Destructor
    SessionData() noexcept;
    ~SessionData() noexcept;
    
    // Disable copy
    SessionData(const SessionData&) = delete;
    SessionData& operator=(const SessionData&) = delete;
    
    // Enable move
    SessionData(SessionData&& other) noexcept;
    SessionData& operator=(SessionData&& other) noexcept;
    
    // Session lifecycle
    bool initialize(const SessionConfiguration& cfg, MemoryPool* pool) noexcept;
    bool activate() noexcept;
    bool pause() noexcept;
    bool resume() noexcept;
    bool close() noexcept;
    void reset() noexcept;
    
    // State queries
    bool isActive() const noexcept;
    bool isPaused() const noexcept;
    bool isClosed() const noexcept;
    bool hasFlag(SessionFlags flag) const noexcept;
    
    // Flag operations
    void setFlag(SessionFlags flag) noexcept;
    void clearFlag(SessionFlags flag) noexcept;
    void toggleFlag(SessionFlags flag) noexcept;
    
    // Heartbeat
    void updateHeartbeat() noexcept;
    bool isAlive(u64 timeoutNanos) const noexcept;
    
    // Statistics
    void recordTick() noexcept;
    void recordOrder(bool executed) noexcept;
    void recordError() noexcept;
    
    // Memory allocation (from session pool)
    void* allocate(usize size, u32 alignment = CACHE_LINE) noexcept;
    
    // Debug
    void dump() const noexcept;
};

// ============================================================================
// SESSION UTILITIES
// ============================================================================

// Get current time in nanoseconds since epoch
u64 GetCurrentTimeNanos() noexcept;

// Convert session state to string
const char* SessionStateToString(SessionState state) noexcept;

// Validate session configuration
bool ValidateSessionConfig(const SessionConfiguration& config) noexcept;

// ============================================================================
// SESSION INFORMATION EXPORTS
// ============================================================================

extern "C" {
    AARENDOCORE_API const char* AARendoCore_GetSessionInfo();
    AARENDOCORE_API u32 AARendoCore_GetSessionSize();
    // PSYCHOTIC PRECISION: AARendoCore_ValidateSessionId is declared in Core_Types.h
}

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCOREGLM_CORE_SESSION_H