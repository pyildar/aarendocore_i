// Core_SessionManager.cpp - SESSION MANAGER IMPLEMENTATION
// Managing 10M concurrent sessions with EXTREME precision

#include "Core_SessionManager.h"
#include <cstdio>
#include <cstring>
#include <new>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// GLOBAL SESSION MANAGER
// ============================================================================

static SessionManager* g_sessionManager = nullptr;

SessionManager* GetSessionManager() noexcept {
    return g_sessionManager;
}

bool InitializeSessionManager(ThreadPool* threadPool) noexcept {
    if (g_sessionManager) {
        return false;  // Already initialized
    }
    
    g_sessionManager = new(std::nothrow) SessionManager();
    if (!g_sessionManager) {
        return false;
    }
    
    if (!g_sessionManager->initialize(threadPool)) {
        delete g_sessionManager;
        g_sessionManager = nullptr;
        return false;
    }
    
    return true;
}

void ShutdownSessionManager() noexcept {
    if (g_sessionManager) {
        g_sessionManager->shutdown();
        delete g_sessionManager;
        g_sessionManager = nullptr;
    }
}

// ============================================================================
// SESSION MANAGER STATISTICS IMPLEMENTATION
// ============================================================================

SessionManagerStats::SessionManagerStats() noexcept {
    reset();
}

void SessionManagerStats::reset() noexcept {
    totalSessionsCreated.store(0, MemoryOrderRelaxed);
    totalSessionsDestroyed.store(0, MemoryOrderRelaxed);
    activeSessions.store(0, MemoryOrderRelaxed);
    pausedSessions.store(0, MemoryOrderRelaxed);
    errorSessions.store(0, MemoryOrderRelaxed);
    
    sessionCreationTime.store(0, MemoryOrderRelaxed);
    sessionDestructionTime.store(0, MemoryOrderRelaxed);
    sessionLookupTime.store(0, MemoryOrderRelaxed);
    
    totalMemoryAllocated.store(0, MemoryOrderRelaxed);
    totalMemoryFreed.store(0, MemoryOrderRelaxed);
    
    for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
        sessionsPerNode[i].store(0, MemoryOrderRelaxed);
        memoryPerNode[i].store(0, MemoryOrderRelaxed);
    }
}

// ============================================================================
// SESSION BUCKET IMPLEMENTATION
// ============================================================================

SessionBucket::SessionBucket() noexcept {
    for (u32 i = 0; i < BUCKET_SIZE; ++i) {
        sessions[i].store(nullptr, MemoryOrderRelaxed);
    }
    count.store(0, MemoryOrderRelaxed);
    version.store(0, MemoryOrderRelaxed);
}

SessionBucket::~SessionBucket() noexcept {
    clear();
}

SessionData* SessionBucket::find(SessionId id) const noexcept {
    u32 currentCount = count.load(MemoryOrderAcquire);
    
    for (u32 i = 0; i < currentCount && i < BUCKET_SIZE; ++i) {
        SessionData* session = sessions[i].load(MemoryOrderAcquire);
        if (session && session->id.value == id.value) {
            return session;
        }
    }
    
    return nullptr;
}

bool SessionBucket::insert(SessionData* session) noexcept {
    if (!session) {
        return false;
    }
    
    lock.lock();
    
    u32 currentCount = count.load(MemoryOrderRelaxed);
    if (currentCount >= BUCKET_SIZE) {
        lock.unlock();
        return false;  // Bucket full
    }
    
    // Find empty slot
    for (u32 i = 0; i < BUCKET_SIZE; ++i) {
        SessionData* expected = nullptr;
        if (sessions[i].compare_exchange_strong(expected, session,
                                                MemoryOrderRelease, MemoryOrderRelaxed)) {
            count.fetch_add(1, MemoryOrderRelease);
            version.fetch_add(1, MemoryOrderRelease);
            lock.unlock();
            return true;
        }
    }
    
    lock.unlock();
    return false;
}

bool SessionBucket::remove(SessionId id) noexcept {
    lock.lock();
    
    for (u32 i = 0; i < BUCKET_SIZE; ++i) {
        SessionData* session = sessions[i].load(MemoryOrderAcquire);
        if (session && session->id.value == id.value) {
            sessions[i].store(nullptr, MemoryOrderRelease);
            count.fetch_sub(1, MemoryOrderRelease);
            version.fetch_add(1, MemoryOrderRelease);
            lock.unlock();
            return true;
        }
    }
    
    lock.unlock();
    return false;
}

void SessionBucket::clear() noexcept {
    lock.lock();
    
    for (u32 i = 0; i < BUCKET_SIZE; ++i) {
        sessions[i].store(nullptr, MemoryOrderRelease);
    }
    
    count.store(0, MemoryOrderRelease);
    version.fetch_add(1, MemoryOrderRelease);
    
    lock.unlock();
}

// ============================================================================
// SESSION TABLE IMPLEMENTATION
// ============================================================================

SessionTable::SessionTable() noexcept : buckets_(nullptr) {
}

SessionTable::~SessionTable() noexcept {
    clear();
    if (buckets_) {
        FreeAligned(buckets_);
        buckets_ = nullptr;
    }
}

bool SessionTable::initialize() noexcept {
    if (buckets_) {
        return false;  // Already initialized
    }
    
    // Allocate bucket array
    buckets_ = static_cast<SessionBucket*>(
        AllocateAligned(sizeof(SessionBucket) * TABLE_SIZE, CACHE_LINE)
    );
    
    if (!buckets_) {
        return false;
    }
    
    // Initialize buckets
    for (u32 i = 0; i < TABLE_SIZE; ++i) {
        new(&buckets_[i]) SessionBucket();
    }
    
    totalSessions_.store(0, MemoryOrderRelaxed);
    totalLookups_.store(0, MemoryOrderRelaxed);
    totalCollisions_.store(0, MemoryOrderRelaxed);
    
    return true;
}

SessionData* SessionTable::find(SessionId id) const noexcept {
    if (!buckets_) {
        return nullptr;
    }
    
    AtomicIncrement(const_cast<AtomicU64&>(totalLookups_));
    
    u32 bucketIndex = hash(id);
    return buckets_[bucketIndex].find(id);
}

bool SessionTable::insert(SessionData* session) noexcept {
    if (!buckets_ || !session) {
        return false;
    }
    
    u32 bucketIndex = hash(session->id);
    
    if (buckets_[bucketIndex].insert(session)) {
        AtomicIncrement(totalSessions_);
        return true;
    }
    
    // Collision - try linear probing
    AtomicIncrement(totalCollisions_);
    
    for (u32 i = 1; i < 16; ++i) {
        u32 probedIndex = (bucketIndex + i) & TABLE_MASK;
        if (buckets_[probedIndex].insert(session)) {
            AtomicIncrement(totalSessions_);
            return true;
        }
    }
    
    return false;  // Table full in this region
}

bool SessionTable::remove(SessionId id) noexcept {
    if (!buckets_) {
        return false;
    }
    
    u32 bucketIndex = hash(id);
    
    if (buckets_[bucketIndex].remove(id)) {
        AtomicDecrement(totalSessions_);
        return true;
    }
    
    // Check probed locations
    for (u32 i = 1; i < 16; ++i) {
        u32 probedIndex = (bucketIndex + i) & TABLE_MASK;
        if (buckets_[probedIndex].remove(id)) {
            AtomicDecrement(totalSessions_);
            return true;
        }
    }
    
    return false;
}

void SessionTable::clear() noexcept {
    if (buckets_) {
        for (u32 i = 0; i < TABLE_SIZE; ++i) {
            buckets_[i].clear();
        }
        totalSessions_.store(0, MemoryOrderRelaxed);
    }
}

u32 SessionTable::hash(SessionId id) const noexcept {
    // MurmurHash3 inspired mixing
    u64 h = id.value;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<u32>(h) & TABLE_MASK;
}

// ============================================================================
// SESSION POOL IMPLEMENTATION
// ============================================================================

SessionPool::SessionPool() noexcept 
    : poolSize_(0), nodeId_(0), sessions_(nullptr), 
      freeList_(nullptr), nodes_(nullptr) {
}

SessionPool::~SessionPool() noexcept {
    release();
}

bool SessionPool::initialize(u32 size, u32 nodeId) noexcept {
    if (sessions_) {
        return false;  // Already initialized
    }
    
    poolSize_ = size;
    nodeId_ = nodeId;
    
    // Allocate session array on specific NUMA node
    sessions_ = static_cast<SessionData*>(
        AllocateOnNumaNode(nodeId, sizeof(SessionData) * size, ULTRA_PAGE)
    );
    
    if (!sessions_) {
        return false;
    }
    
    // Allocate free list nodes
    nodes_ = static_cast<FreeNode*>(
        AllocateAligned(sizeof(FreeNode) * size, CACHE_LINE)
    );
    
    if (!nodes_) {
        FreeNumaMemory(sessions_);
        sessions_ = nullptr;
        return false;
    }
    
    // Initialize sessions and build free list
    FreeNode* head = nullptr;
    for (u32 i = 0; i < size; ++i) {
        new(&sessions_[i]) SessionData();
        sessions_[i].sessionIndex = i;
        sessions_[i].numaNode = nodeId;
        
        nodes_[i].session = &sessions_[i];
        nodes_[i].next = head;
        head = &nodes_[i];
    }
    
    freeList_.store(head, MemoryOrderRelease);
    available_.store(size, MemoryOrderRelaxed);
    allocated_.store(0, MemoryOrderRelaxed);
    
    return true;
}

SessionData* SessionPool::allocate() noexcept {
    FreeNode* head = freeList_.load(MemoryOrderAcquire);
    
    while (head) {
        FreeNode* next = head->next;
        
        if (freeList_.compare_exchange_weak(head, next,
                                           MemoryOrderRelease,
                                           MemoryOrderAcquire)) {
            AtomicIncrement(allocated_);
            AtomicDecrement(available_);
            return head->session;
        }
    }
    
    return nullptr;  // Pool exhausted
}

void SessionPool::deallocate(SessionData* session) noexcept {
    if (!session || session < sessions_ || 
        session >= sessions_ + poolSize_) {
        return;  // Not from this pool
    }
    
    // Reset session
    session->reset();
    
    // Find corresponding node
    u32 index = static_cast<u32>(session - sessions_);
    FreeNode* node = &nodes_[index];
    
    // Add back to free list
    FreeNode* head = freeList_.load(MemoryOrderAcquire);
    do {
        node->next = head;
    } while (!freeList_.compare_exchange_weak(head, node,
                                              MemoryOrderRelease,
                                              MemoryOrderAcquire));
    
    AtomicDecrement(allocated_);
    AtomicIncrement(available_);
}

void SessionPool::release() noexcept {
    if (sessions_) {
        // Destroy all sessions
        for (u32 i = 0; i < poolSize_; ++i) {
            sessions_[i].~SessionData();
        }
        
        FreeNumaMemory(sessions_);
        sessions_ = nullptr;
    }
    
    if (nodes_) {
        FreeAligned(nodes_);
        nodes_ = nullptr;
    }
    
    poolSize_ = 0;
    freeList_.store(nullptr, MemoryOrderRelease);
}

// ============================================================================
// SESSION MANAGER IMPLEMENTATION
// ============================================================================

SessionManager::SessionManager() noexcept 
    : numaNodes_(0), threadPool_(nullptr) {
    for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
        pools_[i] = nullptr;
    }
}

SessionManager::~SessionManager() noexcept {
    shutdown();
}

bool SessionManager::initialize(ThreadPool* threadPool) noexcept {
    if (initialized_.load(MemoryOrderAcquire)) {
        return false;  // Already initialized
    }
    
    // Get NUMA configuration
    numaNodes_ = GetNumaNodeCount();
    if (numaNodes_ == 0) {
        numaNodes_ = 1;
    }
    
    // Initialize session table
    if (!sessionTable_.initialize()) {
        return false;
    }
    
    // Initialize memory pool
    u64 memoryPerNode = MEMORY_POOL_SIZE / numaNodes_;
    if (!memoryPool_.initialize(numaNodes_, memoryPerNode)) {
        return false;
    }
    
    // Initialize session pools
    if (!initializePools()) {
        memoryPool_.release();
        return false;
    }
    
    // Set thread pool
    threadPool_ = threadPool;
    
    // Reset statistics
    stats_.reset();
    
    initialized_.store(true, MemoryOrderRelease);
    running_.store(true, MemoryOrderRelease);
    
    return true;
}

void SessionManager::shutdown() noexcept {
    if (!initialized_.exchange(false, MemoryOrderAcqRel)) {
        return;  // Not initialized
    }
    
    running_.store(false, MemoryOrderRelease);
    
    // Clear session table
    sessionTable_.clear();
    
    // Release pools
    releasePools();
    
    // Release memory pool
    memoryPool_.release();
    
    threadPool_ = nullptr;
}

SessionId SessionManager::createSession(const SessionConfiguration& config) noexcept {
    if (!running_.load(MemoryOrderAcquire)) {
        return SessionId{0};
    }
    
    u64 startTime = GetCurrentTimeNanos();
    
    // Validate configuration
    if (!ValidateSessionConfig(config)) {
        return SessionId{0};
    }
    
    // Select pool based on configuration
    SessionPool* pool = pools_[config.numaNode % numaNodes_];
    if (!pool) {
        return SessionId{0};
    }
    
    // Allocate session from pool
    SessionData* session = pool->allocate();
    if (!session) {
        return SessionId{0};  // Pool exhausted
    }
    
    // Generate unique session ID
    SessionId id = GenerateSessionId(nextSessionId_.next());
    session->id = id;
    
    // Allocate memory pool for session
    usize poolSize = config.maxMemoryUsage;
    void* poolMemory = memoryPool_.allocateOnNode(config.numaNode, poolSize, PAGE_SIZE);
    
    if (!poolMemory) {
        pool->deallocate(session);
        return SessionId{0};
    }
    
    // Create session memory pool
    MemoryPool* sessionPool = new(poolMemory) MemoryPool();
    if (!sessionPool->initialize(poolSize - sizeof(MemoryPool), CACHE_LINE)) {
        memoryPool_.reset();  // Return memory
        pool->deallocate(session);
        return SessionId{0};
    }
    
    // Initialize session
    if (!session->initialize(config, sessionPool)) {
        sessionPool->release();
        memoryPool_.reset();
        pool->deallocate(session);
        return SessionId{0};
    }
    
    // Add to lookup table
    if (!sessionTable_.insert(session)) {
        session->close();
        sessionPool->release();
        memoryPool_.reset();
        pool->deallocate(session);
        return SessionId{0};
    }
    
    // Update statistics
    AtomicIncrement(stats_.totalSessionsCreated);
    AtomicIncrement(stats_.activeSessions);
    AtomicIncrement(stats_.sessionsPerNode[config.numaNode]);
    AtomicAdd(stats_.memoryPerNode[config.numaNode], poolSize);
    AtomicAdd(stats_.totalMemoryAllocated, poolSize);
    
    u64 endTime = GetCurrentTimeNanos();
    AtomicAdd(stats_.sessionCreationTime, endTime - startTime);
    
    return id;
}

SessionData* SessionManager::getSession(SessionId id) noexcept {
    if (!running_.load(MemoryOrderAcquire)) {
        return nullptr;
    }
    
    u64 startTime = GetCurrentTimeNanos();
    SessionData* session = sessionTable_.find(id);
    
    u64 endTime = GetCurrentTimeNanos();
    AtomicAdd(stats_.sessionLookupTime, endTime - startTime);
    
    return session;
}

const SessionData* SessionManager::getSession(SessionId id) const noexcept {
    if (!running_.load(MemoryOrderAcquire)) {
        return nullptr;
    }
    
    return sessionTable_.find(id);
}

bool SessionManager::destroySession(SessionId id) noexcept {
    if (!running_.load(MemoryOrderAcquire)) {
        return false;
    }
    
    u64 startTime = GetCurrentTimeNanos();
    
    // Find session
    SessionData* session = sessionTable_.find(id);
    if (!session) {
        return false;
    }
    
    // Remove from table
    if (!sessionTable_.remove(id)) {
        return false;
    }
    
    // Get session's NUMA node
    u32 nodeId = session->numaNode;
    
    // Close session
    session->close();
    
    // Release session memory
    if (session->memoryPool) {
        usize poolSize = session->config.maxMemoryUsage;
        session->memoryPool->release();
        session->memoryPool->~MemoryPool();
        memoryPool_.reset();
        
        AtomicAdd(stats_.totalMemoryFreed, poolSize);
        // PSYCHOTIC PRECISION: Can't add negative to AtomicU64, use fetch_sub
        stats_.memoryPerNode[nodeId].fetch_sub(poolSize, MemoryOrderRelaxed);
    }
    
    // Return to pool
    if (nodeId < numaNodes_ && pools_[nodeId]) {
        pools_[nodeId]->deallocate(session);
    }
    
    // Update statistics
    AtomicIncrement(stats_.totalSessionsDestroyed);
    AtomicDecrement(stats_.activeSessions);
    AtomicDecrement(stats_.sessionsPerNode[nodeId]);
    
    u64 endTime = GetCurrentTimeNanos();
    AtomicAdd(stats_.sessionDestructionTime, endTime - startTime);
    
    return true;
}

u32 SessionManager::createSessions(const SessionConfiguration& config,
                                   SessionId* ids, u32 count) noexcept {
    if (!running_.load(MemoryOrderAcquire) || !ids || count == 0) {
        return 0;
    }
    
    u32 created = 0;
    for (u32 i = 0; i < count; ++i) {
        ids[i] = createSession(config);
        if (ids[i].value != 0) {
            ++created;
        }
    }
    
    return created;
}

u32 SessionManager::destroySessions(const SessionId* ids, u32 count) noexcept {
    if (!running_.load(MemoryOrderAcquire) || !ids || count == 0) {
        return 0;
    }
    
    u32 destroyed = 0;
    for (u32 i = 0; i < count; ++i) {
        if (destroySession(ids[i])) {
            ++destroyed;
        }
    }
    
    return destroyed;
}

u64 SessionManager::getActiveSessionCount() const noexcept {
    return stats_.activeSessions.load(MemoryOrderRelaxed);
}

u64 SessionManager::getTotalSessionCount() const noexcept {
    return sessionTable_.getTotalSessions();
}

u64 SessionManager::getSessionCountForNode(u32 nodeId) const noexcept {
    if (nodeId >= MAX_NUMA_NODES) {
        return 0;
    }
    return stats_.sessionsPerNode[nodeId].load(MemoryOrderRelaxed);
}

bool SessionManager::allocateSessionMemory(SessionId id, usize size) noexcept {
    SessionData* session = getSession(id);
    if (!session || !session->memoryPool) {
        return false;
    }
    
    void* memory = session->allocate(size);
    return memory != nullptr;
}

void SessionManager::releaseSessionMemory(SessionId id) noexcept {
    SessionData* session = getSession(id);
    if (session && session->memoryPool) {
        session->memoryPool->reset();
    }
}

u32 SessionManager::cleanupInactiveSessions(u64 timeoutNanos) noexcept {
    // This would iterate through all sessions and clean up inactive ones
    // For now, return 0 as placeholder
    (void)timeoutNanos;
    return 0;
}

void SessionManager::defragmentPools() noexcept {
    // This would defragment the memory pools
    // Placeholder for now
}

void SessionManager::dumpState() const noexcept {
    std::printf("SessionManager State:\n");
    std::printf("  Initialized: %s\n", initialized_.load() ? "Yes" : "No");
    std::printf("  Running: %s\n", running_.load() ? "Yes" : "No");
    std::printf("  NUMA Nodes: %u\n", numaNodes_);
    std::printf("  Active Sessions: %llu\n", 
        static_cast<unsigned long long>(getActiveSessionCount()));
    std::printf("  Total Sessions: %llu\n", 
        static_cast<unsigned long long>(getTotalSessionCount()));
    
    for (u32 i = 0; i < numaNodes_; ++i) {
        std::printf("  Node %u: %llu sessions, %llu bytes\n", i,
            static_cast<unsigned long long>(stats_.sessionsPerNode[i].load()),
            static_cast<unsigned long long>(stats_.memoryPerNode[i].load()));
    }
}

bool SessionManager::initializePools() noexcept {
    u32 sessionsPerNode = SESSIONS_PER_NUMA_NODE;
    
    for (u32 i = 0; i < numaNodes_; ++i) {
        pools_[i] = new(std::nothrow) SessionPool();
        if (!pools_[i]) {
            releasePools();
            return false;
        }
        
        if (!pools_[i]->initialize(sessionsPerNode, i)) {
            releasePools();
            return false;
        }
    }
    
    return true;
}

void SessionManager::releasePools() noexcept {
    for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
        if (pools_[i]) {
            pools_[i]->release();
            delete pools_[i];
            pools_[i] = nullptr;
        }
    }
}

SessionPool* SessionManager::selectPool() noexcept {
    // Round-robin selection
    static AtomicU32 nextPool{0};
    u32 poolId = nextPool.fetch_add(1, MemoryOrderRelaxed) % numaNodes_;
    return pools_[poolId];
}

SessionPool* SessionManager::getPoolForNode(u32 nodeId) noexcept {
    if (nodeId >= numaNodes_) {
        return nullptr;
    }
    return pools_[nodeId];
}

// ============================================================================
// SESSION MANAGER EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API bool AARendoCore_InitializeSessionManager() {
    return InitializeSessionManager(nullptr);
}

extern "C" AARENDOCORE_API void AARendoCore_ShutdownSessionManager() {
    ShutdownSessionManager();
}

extern "C" AARENDOCORE_API u64 AARendoCore_CreateSession(const char* accountId,
                                                         const char* strategyName) {
    SessionManager* mgr = GetSessionManager();
    if (!mgr) {
        return 0;
    }
    
    SessionConfiguration config;
    config.setDefaults();
    
    if (accountId) {
        std::strncpy(config.accountId, accountId, MAX_ACCOUNT_ID_LENGTH - 1);
        config.accountId[MAX_ACCOUNT_ID_LENGTH - 1] = '\0'; // PSYCHOTIC NULL TERMINATION
    }
    
    if (strategyName) {
        std::strncpy(config.strategyName, strategyName, MAX_STRATEGY_NAME_LENGTH - 1);
        config.strategyName[MAX_STRATEGY_NAME_LENGTH - 1] = '\0'; // PSYCHOTIC NULL TERMINATION
    }
    
    SessionId id = mgr->createSession(config);
    return id.value;
}

extern "C" AARENDOCORE_API bool AARendoCore_DestroySession(u64 sessionId) {
    SessionManager* mgr = GetSessionManager();
    if (!mgr) {
        return false;
    }
    
    return mgr->destroySession(SessionId{sessionId});
}

extern "C" AARENDOCORE_API u64 AARendoCore_GetActiveSessionCount() {
    SessionManager* mgr = GetSessionManager();
    if (!mgr) {
        return 0;
    }
    
    return mgr->getActiveSessionCount();
}

extern "C" AARENDOCORE_API const char* AARendoCore_GetSessionManagerInfo() {
    static char info[512];
    
    SessionManager* mgr = GetSessionManager();
    if (!mgr) {
        std::snprintf(info, sizeof(info), "SessionManager: Not initialized");
    } else {
        std::snprintf(info, sizeof(info),
            "SessionManager: Active=%llu, Total=%llu, Created=%llu, Destroyed=%llu",
            static_cast<unsigned long long>(mgr->getActiveSessionCount()),
            static_cast<unsigned long long>(mgr->getTotalSessionCount()),
            static_cast<unsigned long long>(mgr->getStats().totalSessionsCreated.load()),
            static_cast<unsigned long long>(mgr->getStats().totalSessionsDestroyed.load())
        );
    }
    
    return info;
}

AARENDOCORE_NAMESPACE_END