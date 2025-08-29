// Core_Session.cpp - SESSION IMPLEMENTATION
// Managing individual trading sessions with EXTREME precision

#include "Core_Session.h"
#include <cstdio>
#include <cstring>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// SESSION STATISTICS IMPLEMENTATION
// ============================================================================

SessionStatistics::SessionStatistics() noexcept {
    reset();
}

void SessionStatistics::reset() noexcept {
    creationTime = GetCurrentTimeNanos();
    lastActivityTime = creationTime;
    totalProcessingTime = 0;
    
    ticksProcessed.store(0, MemoryOrderRelaxed);
    ordersSubmitted.store(0, MemoryOrderRelaxed);
    ordersExecuted.store(0, MemoryOrderRelaxed);
    ordersCancelled.store(0, MemoryOrderRelaxed);
    
    messagesSent.store(0, MemoryOrderRelaxed);
    messagesReceived.store(0, MemoryOrderRelaxed);
    bytesTransferred.store(0, MemoryOrderRelaxed);
    errorCount.store(0, MemoryOrderRelaxed);
    
    cacheHits.store(0, MemoryOrderRelaxed);
    cacheMisses.store(0, MemoryOrderRelaxed);
}

// ============================================================================
// SESSION CONFIGURATION IMPLEMENTATION
// ============================================================================

SessionConfiguration::SessionConfiguration() noexcept {
    setDefaults();
}

void SessionConfiguration::setDefaults() noexcept {
    // Clear identity strings
    std::memset(accountId, 0, sizeof(accountId));
    std::memset(strategyName, 0, sizeof(strategyName));
    
    // Default resource limits
    maxOrdersPerSecond = 1000;
    maxPositions = 100;
    maxPendingOrders = 50;
    maxMemoryUsage = 100 * 1024 * 1024;  // 100MB default
    
    // Default queue sizes
    tickQueueSize = TICK_QUEUE_SIZE;
    orderQueueSize = ORDER_QUEUE_SIZE;
    eventQueueSize = EVENT_QUEUE_SIZE;
    numaNode = 0;
    
    // Default flags
    flags = SESSION_FLAG_NUMA_AWARE;
}

// ============================================================================
// SESSION DATA IMPLEMENTATION
// ============================================================================

SessionData::SessionData() noexcept 
    : id{0}, sessionIndex(0), numaNode(0),
      memoryPool(nullptr), cpuAffinity(0), workerId(0),
      createdAt(0), lastTickAt(0), lastHeartbeatAt(0),
      userData(nullptr) {
    state.store(SessionState::Uninitialized, MemoryOrderRelaxed);
    flags.store(SESSION_FLAG_NONE, MemoryOrderRelaxed);
}

SessionData::~SessionData() noexcept {
    close();
}

SessionData::SessionData(SessionData&& other) noexcept 
    : id(other.id), sessionIndex(other.sessionIndex), numaNode(other.numaNode),
      config(other.config), // config is POD, can copy
      memoryPool(other.memoryPool), cpuAffinity(other.cpuAffinity),
      workerId(other.workerId), createdAt(other.createdAt),
      lastTickAt(other.lastTickAt), lastHeartbeatAt(other.lastHeartbeatAt),
      userData(other.userData) {
    
    // PSYCHOTIC PRECISION: Can't copy stats with atomics, must manually transfer
    stats.creationTime = other.stats.creationTime;
    stats.lastActivityTime = other.stats.lastActivityTime;
    stats.totalProcessingTime = other.stats.totalProcessingTime;
    
    // Transfer atomic values with EXTREME PRECISION
    stats.ticksProcessed.store(other.stats.ticksProcessed.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.ordersSubmitted.store(other.stats.ordersSubmitted.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.ordersExecuted.store(other.stats.ordersExecuted.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.ordersCancelled.store(other.stats.ordersCancelled.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.messagesSent.store(other.stats.messagesSent.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.messagesReceived.store(other.stats.messagesReceived.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.bytesTransferred.store(other.stats.bytesTransferred.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.errorCount.store(other.stats.errorCount.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.cacheHits.store(other.stats.cacheHits.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    stats.cacheMisses.store(other.stats.cacheMisses.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
    
    state.store(other.state.load(MemoryOrderAcquire), MemoryOrderRelease);
    flags.store(other.flags.load(MemoryOrderAcquire), MemoryOrderRelease);
    
    // Clear the moved-from object
    other.id = SessionId{0};
    other.memoryPool = nullptr;
    other.userData = nullptr;
    other.state.store(SessionState::Uninitialized, MemoryOrderRelease);
}

SessionData& SessionData::operator=(SessionData&& other) noexcept {
    if (this != &other) {
        // Close current session
        close();
        
        // Move data
        id = other.id;
        sessionIndex = other.sessionIndex;
        numaNode = other.numaNode;
        config = other.config;
        
        // PSYCHOTIC PRECISION: Manually transfer stats with atomics
        stats.creationTime = other.stats.creationTime;
        stats.lastActivityTime = other.stats.lastActivityTime;
        stats.totalProcessingTime = other.stats.totalProcessingTime;
        
        stats.ticksProcessed.store(other.stats.ticksProcessed.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.ordersSubmitted.store(other.stats.ordersSubmitted.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.ordersExecuted.store(other.stats.ordersExecuted.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.ordersCancelled.store(other.stats.ordersCancelled.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.messagesSent.store(other.stats.messagesSent.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.messagesReceived.store(other.stats.messagesReceived.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.bytesTransferred.store(other.stats.bytesTransferred.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.errorCount.store(other.stats.errorCount.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.cacheHits.store(other.stats.cacheHits.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        stats.cacheMisses.store(other.stats.cacheMisses.load(MemoryOrderRelaxed), MemoryOrderRelaxed);
        
        memoryPool = other.memoryPool;
        cpuAffinity = other.cpuAffinity;
        workerId = other.workerId;
        createdAt = other.createdAt;
        lastTickAt = other.lastTickAt;
        lastHeartbeatAt = other.lastHeartbeatAt;
        userData = other.userData;
        
        state.store(other.state.load(MemoryOrderAcquire), MemoryOrderRelease);
        flags.store(other.flags.load(MemoryOrderAcquire), MemoryOrderRelease);
        
        // Clear the moved-from object
        other.id = SessionId{0};
        other.memoryPool = nullptr;
        other.userData = nullptr;
        other.state.store(SessionState::Uninitialized, MemoryOrderRelease);
    }
    return *this;
}

bool SessionData::initialize(const SessionConfiguration& cfg, MemoryPool* pool) noexcept {
    // Check if already initialized
    SessionState expected = SessionState::Uninitialized;
    if (!state.compare_exchange_strong(expected, SessionState::Initializing,
                                       MemoryOrderAcqRel, MemoryOrderAcquire)) {
        return false;
    }
    
    // Set configuration
    config = cfg;
    memoryPool = pool;
    numaNode = cfg.numaNode;
    flags.store(cfg.flags, MemoryOrderRelease);
    
    // Initialize timestamps
    createdAt = GetCurrentTimeNanos();
    lastTickAt = createdAt;
    lastHeartbeatAt = createdAt;
    
    // Reset statistics
    stats.reset();
    
    // Transition to active state
    state.store(SessionState::Active, MemoryOrderRelease);
    
    return true;
}

bool SessionData::activate() noexcept {
    SessionState expected = SessionState::Paused;
    if (state.compare_exchange_strong(expected, SessionState::Active,
                                      MemoryOrderAcqRel, MemoryOrderAcquire)) {
        updateHeartbeat();
        return true;
    }
    return false;
}

bool SessionData::pause() noexcept {
    SessionState expected = SessionState::Active;
    return state.compare_exchange_strong(expected, SessionState::Paused,
                                         MemoryOrderAcqRel, MemoryOrderAcquire);
}

bool SessionData::resume() noexcept {
    return activate();
}

bool SessionData::close() noexcept {
    SessionState current = state.load(MemoryOrderAcquire);
    
    // Can't close if already closed or uninitialized
    if (current == SessionState::Closed || current == SessionState::Uninitialized) {
        return false;
    }
    
    // Transition to closing state
    SessionState expected = current;
    if (!state.compare_exchange_strong(expected, SessionState::Closing,
                                       MemoryOrderAcqRel, MemoryOrderAcquire)) {
        return false;
    }
    
    // Clear user data
    userData = nullptr;
    
    // Final state transition
    state.store(SessionState::Closed, MemoryOrderRelease);
    
    return true;
}

void SessionData::reset() noexcept {
    // Reset to uninitialized state
    state.store(SessionState::Uninitialized, MemoryOrderRelease);
    flags.store(SESSION_FLAG_NONE, MemoryOrderRelease);
    
    // Clear data
    id = SessionId{0};
    sessionIndex = 0;
    memoryPool = nullptr;
    userData = nullptr;
    
    // Reset statistics
    stats.reset();
}

bool SessionData::isActive() const noexcept {
    return state.load(MemoryOrderAcquire) == SessionState::Active;
}

bool SessionData::isPaused() const noexcept {
    return state.load(MemoryOrderAcquire) == SessionState::Paused;
}

bool SessionData::isClosed() const noexcept {
    SessionState current = state.load(MemoryOrderAcquire);
    return current == SessionState::Closed || current == SessionState::Closing;
}

bool SessionData::hasFlag(SessionFlags flag) const noexcept {
    return (flags.load(MemoryOrderAcquire) & flag) != 0;
}

void SessionData::setFlag(SessionFlags flag) noexcept {
    flags.fetch_or(flag, MemoryOrderAcqRel);
}

void SessionData::clearFlag(SessionFlags flag) noexcept {
    flags.fetch_and(~flag, MemoryOrderAcqRel);
}

void SessionData::toggleFlag(SessionFlags flag) noexcept {
    flags.fetch_xor(flag, MemoryOrderAcqRel);
}

void SessionData::updateHeartbeat() noexcept {
    lastHeartbeatAt = GetCurrentTimeNanos();
}

bool SessionData::isAlive(u64 timeoutNanos) const noexcept {
    u64 now = GetCurrentTimeNanos();
    return (now - lastHeartbeatAt) < timeoutNanos;
}

void SessionData::recordTick() noexcept {
    lastTickAt = GetCurrentTimeNanos();
    stats.lastActivityTime = lastTickAt;
    AtomicIncrement(stats.ticksProcessed);
}

void SessionData::recordOrder(bool executed) noexcept {
    AtomicIncrement(stats.ordersSubmitted);
    if (executed) {
        AtomicIncrement(stats.ordersExecuted);
    }
}

void SessionData::recordError() noexcept {
    AtomicIncrement(stats.errorCount);
}

void* SessionData::allocate(usize size, u32 alignment) noexcept {
    if (memoryPool) {
        return memoryPool->allocate(size, alignment);
    }
    return nullptr;
}

void SessionData::dump() const noexcept {
    std::printf("Session[%llu]: State=%s, Index=%u, Node=%u, Flags=0x%08X\n",
        static_cast<unsigned long long>(id.value),
        SessionStateToString(state.load(MemoryOrderAcquire)),
        sessionIndex, numaNode,
        flags.load(MemoryOrderAcquire));
    
    std::printf("  Config: Account=%s, Strategy=%s, MaxOrders/s=%u\n",
        config.accountId, config.strategyName, config.maxOrdersPerSecond);
    
    std::printf("  Stats: Ticks=%llu, Orders=%llu/%llu, Errors=%llu\n",
        static_cast<unsigned long long>(stats.ticksProcessed.load()),
        static_cast<unsigned long long>(stats.ordersExecuted.load()),
        static_cast<unsigned long long>(stats.ordersSubmitted.load()),
        static_cast<unsigned long long>(stats.errorCount.load()));
}

// ============================================================================
// SESSION UTILITIES IMPLEMENTATION
// ============================================================================

u64 GetCurrentTimeNanos() noexcept {
    using namespace std::chrono;
    return static_cast<u64>(
        duration_cast<nanoseconds>(
            high_resolution_clock::now().time_since_epoch()
        ).count()
    );
}

const char* SessionStateToString(SessionState state) noexcept {
    switch (state) {
        case SessionState::Uninitialized: return "Uninitialized";
        case SessionState::Initializing:  return "Initializing";
        case SessionState::Active:        return "Active";
        case SessionState::Paused:        return "Paused";
        case SessionState::Closing:       return "Closing";
        case SessionState::Closed:        return "Closed";
        case SessionState::Error:         return "Error";
        default:                          return "Unknown";
    }
}

bool ValidateSessionConfig(const SessionConfiguration& config) noexcept {
    // Validate resource limits
    if (config.maxOrdersPerSecond == 0 || config.maxOrdersPerSecond > 100000) {
        return false;
    }
    
    if (config.maxPositions == 0 || config.maxPositions > 10000) {
        return false;
    }
    
    if (config.maxPendingOrders == 0 || config.maxPendingOrders > 10000) {
        return false;
    }
    
    if (config.maxMemoryUsage < 1024 * 1024) {  // Minimum 1MB
        return false;
    }
    
    // Validate queue sizes (must be power of 2)
    if (config.tickQueueSize == 0 || (config.tickQueueSize & (config.tickQueueSize - 1)) != 0) {
        return false;
    }
    
    if (config.orderQueueSize == 0 || (config.orderQueueSize & (config.orderQueueSize - 1)) != 0) {
        return false;
    }
    
    if (config.eventQueueSize == 0 || (config.eventQueueSize & (config.eventQueueSize - 1)) != 0) {
        return false;
    }
    
    // Validate NUMA node
    if (config.numaNode >= MAX_NUMA_NODES) {
        return false;
    }
    
    return true;
}

// ============================================================================
// SESSION INFORMATION EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetSessionInfo() {
    static char info[512];
    
    std::snprintf(info, sizeof(info),
        "Session: Size=%zu bytes, Alignment=%zu, MaxSessions=%llu",
        sizeof(SessionData),
        alignof(SessionData),
        static_cast<unsigned long long>(MAX_CONCURRENT_SESSIONS)
    );
    
    return info;
}

extern "C" AARENDOCORE_API u32 AARendoCore_GetSessionSize() {
    return static_cast<u32>(sizeof(SessionData));
}

// PSYCHOTIC PRECISION: AARendoCore_ValidateSessionId is defined in Core_Types.cpp
// to avoid linker error LNK2005 (multiple definitions)

AARENDOCORE_NAMESPACE_END