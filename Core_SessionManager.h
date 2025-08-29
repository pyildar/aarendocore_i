// Core_SessionManager.h - SESSION MANAGER FOR 10M SESSIONS
// COMPILER PROCESSES AFTER Core_Session
// Manages lifecycle of 10M concurrent trading sessions
// NUMA-aware, lock-free, with PSYCHOTIC isolation

#ifndef AARENDOCOREGLM_CORE_SESSIONMANAGER_H
#define AARENDOCOREGLM_CORE_SESSIONMANAGER_H

#include "Core_Platform.h"
#include "Core_Types.h"
#include "Core_Config.h"
#include "Core_Alignment.h"
#include "Core_Atomic.h"
#include "Core_Memory.h"
#include "Core_NUMA.h"
#include "Core_Threading.h"
#include "Core_Session.h"

#include <memory>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// SESSION MANAGER STATISTICS
// ============================================================================

struct alignas(CACHE_LINE) SessionManagerStats {
    // Session counts
    AtomicU64 totalSessionsCreated{0};
    AtomicU64 totalSessionsDestroyed{0};
    AtomicU64 activeSessions{0};
    AtomicU64 pausedSessions{0};
    AtomicU64 errorSessions{0};
    
    // Performance metrics
    AtomicU64 sessionCreationTime{0};  // Total nanoseconds
    AtomicU64 sessionDestructionTime{0};
    AtomicU64 sessionLookupTime{0};
    
    // Resource usage
    AtomicU64 totalMemoryAllocated{0};
    AtomicU64 totalMemoryFreed{0};
    
    // Per-NUMA node statistics
    AtomicU64 sessionsPerNode[MAX_NUMA_NODES];
    AtomicU64 memoryPerNode[MAX_NUMA_NODES];
    
    SessionManagerStats() noexcept;
    void reset() noexcept;
};

// ============================================================================
// SESSION BUCKET - Lock-free hash bucket for sessions
// ============================================================================

struct alignas(CACHE_LINE) SessionBucket {
    static constexpr u32 BUCKET_SIZE = 16;  // Sessions per bucket
    
    // Session pointers (atomic for lock-free access)
    std::atomic<SessionData*> sessions[BUCKET_SIZE];
    
    // Bucket lock (only for insertions/deletions)
    Spinlock lock;
    
    // Bucket statistics
    AtomicU32 count{0};
    AtomicU32 version{0};  // Version counter for ABA prevention
    
    SessionBucket() noexcept;
    ~SessionBucket() noexcept;
    
    // Find session by ID
    SessionData* find(SessionId id) const noexcept;
    
    // Insert session (returns false if bucket full)
    bool insert(SessionData* session) noexcept;
    
    // Remove session
    bool remove(SessionId id) noexcept;
    
    // Clear all sessions
    void clear() noexcept;
};

// ============================================================================
// SESSION TABLE - Hash table for session lookup
// ============================================================================

class SessionTable {
private:
    static constexpr u32 TABLE_SIZE = 1048576;  // 1M buckets for 10M sessions
    static constexpr u32 TABLE_MASK = TABLE_SIZE - 1;
    
    // Bucket array
    SessionBucket* buckets_;
    
    // Table statistics
    AtomicU64 totalSessions_{0};
    AtomicU64 totalLookups_{0};
    AtomicU64 totalCollisions_{0};
    
public:
    SessionTable() noexcept;
    ~SessionTable() noexcept;
    
    // Initialize table
    bool initialize() noexcept;
    
    // Session operations
    SessionData* find(SessionId id) const noexcept;
    bool insert(SessionData* session) noexcept;
    bool remove(SessionId id) noexcept;
    void clear() noexcept;
    
    // Statistics
    u64 getTotalSessions() const noexcept { 
        return totalSessions_.load(MemoryOrderRelaxed); 
    }
    
    u64 getTotalLookups() const noexcept { 
        return totalLookups_.load(MemoryOrderRelaxed); 
    }
    
    u64 getTotalCollisions() const noexcept { 
        return totalCollisions_.load(MemoryOrderRelaxed); 
    }
    
private:
    // Hash function for session ID
    u32 hash(SessionId id) const noexcept;
};

// ============================================================================
// SESSION POOL - Pre-allocated pool of sessions
// ============================================================================

class SessionPool {
private:
    // Pool configuration
    u32 poolSize_;
    u32 nodeId_;
    
    // Session storage
    SessionData* sessions_;
    
    // Free list (lock-free stack)
    struct FreeNode {
        SessionData* session;
        FreeNode* next;
    };
    
    std::atomic<FreeNode*> freeList_;
    FreeNode* nodes_;
    
    // Pool statistics
    AtomicU32 allocated_{0};
    AtomicU32 available_{0};
    
public:
    SessionPool() noexcept;
    ~SessionPool() noexcept;
    
    // Initialize pool for specific NUMA node
    bool initialize(u32 size, u32 nodeId) noexcept;
    
    // Allocate session from pool
    SessionData* allocate() noexcept;
    
    // Return session to pool
    void deallocate(SessionData* session) noexcept;
    
    // Pool statistics
    u32 getAllocated() const noexcept { 
        return allocated_.load(MemoryOrderRelaxed); 
    }
    
    u32 getAvailable() const noexcept { 
        return available_.load(MemoryOrderRelaxed); 
    }
    
    // Release all resources
    void release() noexcept;
};

// ============================================================================
// SESSION MANAGER - Main manager for 10M sessions
// ============================================================================

class SessionManager {
private:
    // Manager state
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    
    // NUMA configuration
    u32 numaNodes_;
    
    // Session pools (one per NUMA node)
    SessionPool* pools_[MAX_NUMA_NODES];
    
    // Memory pools (one per NUMA node)
    NumaMemoryPool memoryPool_;
    
    // Session lookup table
    SessionTable sessionTable_;
    
    // Thread pool for session processing
    ThreadPool* threadPool_;
    
    // Manager statistics
    SessionManagerStats stats_;
    
    // Next session ID generator
    SequenceCounter<u64> nextSessionId_;
    
public:
    SessionManager() noexcept;
    ~SessionManager() noexcept;
    
    // Disable copy
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    
    // Initialize manager
    bool initialize(ThreadPool* threadPool = nullptr) noexcept;
    
    // Shutdown manager
    void shutdown() noexcept;
    
    // Session lifecycle
    SessionId createSession(const SessionConfiguration& config) noexcept;
    SessionData* getSession(SessionId id) noexcept;
    const SessionData* getSession(SessionId id) const noexcept;
    bool destroySession(SessionId id) noexcept;
    
    // Batch operations
    u32 createSessions(const SessionConfiguration& config, 
                       SessionId* ids, u32 count) noexcept;
    u32 destroySessions(const SessionId* ids, u32 count) noexcept;
    
    // Session queries
    u64 getActiveSessionCount() const noexcept;
    u64 getTotalSessionCount() const noexcept;
    u64 getSessionCountForNode(u32 nodeId) const noexcept;
    
    // Resource management
    bool allocateSessionMemory(SessionId id, usize size) noexcept;
    void releaseSessionMemory(SessionId id) noexcept;
    
    // Statistics
    const SessionManagerStats& getStats() const noexcept { return stats_; }
    void resetStats() noexcept { stats_.reset(); }
    
    // Maintenance
    u32 cleanupInactiveSessions(u64 timeoutNanos) noexcept;
    void defragmentPools() noexcept;
    
    // Debug
    void dumpState() const noexcept;
    
private:
    // Internal helpers
    SessionPool* selectPool() noexcept;
    SessionPool* getPoolForNode(u32 nodeId) noexcept;
    bool initializePools() noexcept;
    void releasePools() noexcept;
};

// ============================================================================
// GLOBAL SESSION MANAGER INSTANCE
// ============================================================================

// Get global session manager instance
SessionManager* GetSessionManager() noexcept;

// Initialize global session manager
bool InitializeSessionManager(ThreadPool* threadPool = nullptr) noexcept;

// Shutdown global session manager
void ShutdownSessionManager() noexcept;

// ============================================================================
// SESSION MANAGER EXPORTS
// ============================================================================

extern "C" {
    AARENDOCORE_API bool AARendoCore_InitializeSessionManager();
    AARENDOCORE_API void AARendoCore_ShutdownSessionManager();
    AARENDOCORE_API u64 AARendoCore_CreateSession(const char* accountId, 
                                                   const char* strategyName);
    AARENDOCORE_API bool AARendoCore_DestroySession(u64 sessionId);
    AARENDOCORE_API u64 AARendoCore_GetActiveSessionCount();
    AARENDOCORE_API const char* AARendoCore_GetSessionManagerInfo();
}

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCOREGLM_CORE_SESSIONMANAGER_H