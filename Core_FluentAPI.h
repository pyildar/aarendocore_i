//===--- Core_FluentAPI.h - Fluent Interface API ------------------------===//
//
// COMPILATION LEVEL: 6 (Depends ONLY on Level 5 and below)
// ORIGIN: NEW - Generic fluent interface
// DEPENDENCIES: 
//   - Core_Types.h (LEVEL 1) for u32, f64, Tick, Bar, SessionId
//   - Core_StreamSynchronizer.h (LEVEL 5) for StreamProfile, StreamSynchronizer
// DEPENDENTS: 
//   - Core_NinjaTraderConnection.h (LEVEL 7)
//
// PSYCHOTIC PRECISION: EVERY TYPE TRACED TO ITS ORIGIN
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_FLUENTAPI_H
#define AARENDOCORE_CORE_FLUENTAPI_H

// INCLUDES - EXACT DEPENDENCY TRACING
#include "Core_Platform.h"           // LEVEL 0: AARENDOCORE_API macro (line 38)
#include "Core_Types.h"              // LEVEL 1: u32 (line 26), f64 (line 37), Tick (line 113), Bar (line 134), SessionId (line 215)
#include "Core_StreamSynchronizer.h" // LEVEL 5: StreamProfile (line 70), StreamSynchronizer (line 223)

// STANDARD LIBRARY INCLUDES - EXPLICIT
// PSYCHOTIC PRECISION: NO STD! Using TBB for 10M sessions!
#include <tbb/concurrent_vector.h>   // tbb::concurrent_vector for 10M sessions
#include <tbb/concurrent_hash_map.h> // tbb::concurrent_hash_map for lookups
#include <functional>                // std::function (kept for callbacks only)

// NAMESPACE - USING MACRO FROM Core_Platform.h
AARENDOCORE_NAMESPACE_BEGIN

// ==========================================================================
// FLUENT SESSION - FORWARD DECLARATION
// ==========================================================================
struct FluentSession; // PSYCHOTIC: Struct with public constructor for 10M sessions

// ==========================================================================
// FLUENT CONFIGURATION - NEW STRUCTURE
// ==========================================================================

// Origin: Configuration for fluent API
// Scope: API configuration
// Size: Exactly 64 bytes (1 cache line)
struct alignas(AARENDOCORE_CACHE_LINE_SIZE) FluentConfig {
    // Origin: Member - Max concurrent sessions, Scope: Config lifetime
    u32 maxSessions;         // u32 from Core_Types.h line 26
    
    // Origin: Member - NUMA node affinity, Scope: Config lifetime  
    i32 numaNode;           // i32 from Core_Types.h line 32
    
    // Origin: Member - Enable AVX2, Scope: Config lifetime
    bool enableAVX2;        // bool primitive
    
    // Origin: Member - Enable TBB, Scope: Config lifetime
    bool enableTBB;         // bool primitive
    
    // Origin: Member - Padding for alignment, Scope: Config lifetime
    char _pad1[2];          // 2 bytes padding
    
    // Origin: Member - Stream buffer size, Scope: Config lifetime
    u32 streamBufferSize;   // u32 from Core_Types.h line 26
    
    // Origin: Member - Sync frequency Hz, Scope: Config lifetime
    f64 syncFrequency;      // f64 from Core_Types.h line 37
    
    // Origin: Member - Buffer window nanoseconds, Scope: Config lifetime
    u64 bufferWindowNs;     // u64 from Core_Types.h line 27
    
    // Origin: Member - Max lag nanoseconds, Scope: Config lifetime
    u64 maxLagNs;          // u64 from Core_Types.h line 27
    
    // Origin: Member - Leader detection mode, Scope: Config lifetime
    u32 leaderMode;        // u32 from Core_Types.h line 26
    
    // Padding to exactly 64 bytes
    char padding[16];
};

static_assert(sizeof(FluentConfig) == AARENDOCORE_CACHE_LINE_SIZE, 
              "FluentConfig must be exactly one cache line");

// ==========================================================================
// FLUENT API CLASS - MAIN INTERFACE
// ==========================================================================

// Origin: Main fluent API interface
// Scope: API lifetime
class AARENDOCORE_API FluentAPI {
private:
    // Origin: Member - Configuration, Scope: API lifetime
    FluentConfig config_;
    
    // Origin: Member - Stream synchronizer, Scope: API lifetime
    StreamSynchronizer* synchronizer_;  // PSYCHOTIC: Direct allocation, no std::unique_ptr
    
    // Origin: Member - Active sessions, Scope: API lifetime
    // PSYCHOTIC PRECISION: TBB concurrent_vector for 10M sessions!
    tbb::concurrent_vector<FluentSession> sessions_;
    
    // Origin: Member - Pending stream profiles, Scope: API lifetime
    tbb::concurrent_vector<StreamProfile> pendingProfiles_;
    
    // Origin: Member - Sync callback, Scope: API lifetime
    std::function<void(const SynchronizedOutput&)> onSync_;
    
    // Origin: Member - Tick callback, Scope: API lifetime
    std::function<void(u32, const Tick&)> onTick_;
    
    // Origin: Member - Bar callback, Scope: API lifetime  
    std::function<void(u32, const Bar&)> onBar_;
    
    // Origin: Member - Error callback, Scope: API lifetime
    std::function<void(const char*)> onError_;
    
    // Origin: Member - Built state, Scope: API lifetime
    bool isBuilt_;
    
    // Origin: Member - Started state, Scope: API lifetime
    bool isStarted_;

public:
    // ======================================================================
    // CONSTRUCTOR/DESTRUCTOR
    // ======================================================================
    
    // Origin: Default constructor
    FluentAPI() noexcept;
    
    // Origin: Destructor
    ~FluentAPI() noexcept;
    
    // Delete copy/move - RAII principle
    FluentAPI(const FluentAPI&) = delete;
    FluentAPI& operator=(const FluentAPI&) = delete;
    FluentAPI(FluentAPI&&) = delete;
    FluentAPI& operator=(FluentAPI&&) = delete;
    
    // ======================================================================
    // CONFIGURATION METHODS - CHAINABLE
    // ======================================================================
    
    // Origin: Set max sessions
    // Input: max - Maximum sessions (up to 10M)
    // Output: Reference for chaining
    FluentAPI& withMaxSessions(u32 max) noexcept;
    
    // Origin: Set NUMA node
    // Input: node - NUMA node ID
    // Output: Reference for chaining
    FluentAPI& onNumaNode(i32 node) noexcept;
    
    // Origin: Enable AVX2
    // Input: enable - Enable flag
    // Output: Reference for chaining
    FluentAPI& withAVX2(bool enable = true) noexcept;
    
    // Origin: Enable TBB
    // Input: enable - Enable flag
    // Output: Reference for chaining
    FluentAPI& withTBB(bool enable = true) noexcept;
    
    // Origin: Set sync frequency
    // Input: freq - Frequency in Hz
    // Output: Reference for chaining
    FluentAPI& atFrequency(f64 freq) noexcept;
    
    // Origin: Set buffer window
    // Input: ns - Nanoseconds
    // Output: Reference for chaining
    FluentAPI& withBufferWindow(u64 ns) noexcept;
    
    // ======================================================================
    // STREAM CONFIGURATION - CHAINABLE
    // ======================================================================
    
    // Origin: Add stream with profile
    // Input: profile - Stream profile from Core_StreamSynchronizer.h
    // Output: Reference for chaining
    FluentAPI& addStream(const StreamProfile& profile) noexcept;
    
    // ======================================================================
    // CALLBACK CONFIGURATION - CHAINABLE
    // ======================================================================
    
    // Origin: Set synchronization callback
    // Input: callback - Function to call on sync
    // Output: Reference for chaining
    FluentAPI& onSynchronized(std::function<void(const SynchronizedOutput&)> callback) noexcept;
    
    // Origin: Set tick callback
    // Input: callback - Function to call on tick
    // Output: Reference for chaining  
    FluentAPI& onTick(std::function<void(u32, const Tick&)> callback) noexcept;
    
    // Origin: Set bar callback
    // Input: callback - Function to call on bar
    // Output: Reference for chaining
    FluentAPI& onBar(std::function<void(u32, const Bar&)> callback) noexcept;
    
    // Origin: Set error callback
    // Input: callback - Function to call on error
    // Output: Reference for chaining
    FluentAPI& onError(std::function<void(const char*)> callback) noexcept;
    
    // ======================================================================
    // EXECUTION METHODS
    // ======================================================================
    
    // Origin: Build the system
    // Output: Success/failure
    bool build() noexcept;
    
    // Origin: Start processing
    // Output: Success/failure
    bool start() noexcept;
    
    // Origin: Stop processing
    void stop() noexcept;
    
    // Origin: Process tick for stream
    // Input: streamId - Stream ID, tick - Tick data
    // Output: Success/failure
    bool processTick(u32 streamId, const Tick& tick) noexcept;
    
    // Origin: Process bar for stream
    // Input: streamId - Stream ID, bar - Bar data
    // Output: Success/failure
    bool processBar(u32 streamId, const Bar& bar) noexcept;
    
    // Origin: Force synchronization
    // Output: Success/failure
    bool synchronizeNow() noexcept;
    
    // Origin: Get synchronizer
    // Output: Pointer to synchronizer (can be null)
    StreamSynchronizer* getSynchronizer() noexcept { return synchronizer_; }
    
    // Origin: Get session count
    // Output: Number of active sessions
    u32 getSessionCount() const noexcept;
    
    // ======================================================================
    // SESSION MANAGEMENT
    // ======================================================================
    
    // Origin: Create session
    // Output: Session pointer or nullptr
    FluentSession* createSession() noexcept;
    
    // Origin: Destroy session
    // Input: session - Session to destroy
    void destroySession(FluentSession* session) noexcept;
};

// ==========================================================================
// FLUENT SESSION CLASS - SESSION MANAGEMENT
// ==========================================================================

// Origin: Individual session in the API - STRUCT for 10M sessions!
// Scope: Session lifetime
// PSYCHOTIC: Public struct, no private constructor issues!
struct AARENDOCORE_API FluentSession {
    // Origin: Member - Session ID, Scope: Session lifetime
    SessionId sessionId;  // SessionId from Core_Types.h line 215
    
    // Origin: Member - Parent API, Scope: Session lifetime
    FluentAPI* parent;
    
    // Origin: Member - Active state, Scope: Session lifetime
    AtomicBool active;  // PSYCHOTIC: Atomic for lock-free access
    
    // Origin: Member - Subscribed streams, Scope: Session lifetime
    // PSYCHOTIC: Fixed array for ZERO allocations!
    u32 streamIds[32];  // u32 from Core_Types.h line 26
    AtomicU32 streamCount;  // Track count atomically
    
    // Origin: Default constructor - REQUIRED for TBB concurrent_vector!
    FluentSession() noexcept : sessionId{}, parent{nullptr}, active{false}, streamIds{}, streamCount{0} {}
    
    // Origin: Parameterized constructor - PUBLIC for 10M sessions!
    FluentSession(FluentAPI* p, SessionId id) noexcept;
    
    // PSYCHOTIC: Copy constructor for TBB concurrent_vector - atomics need special handling!
    FluentSession(const FluentSession& other) noexcept 
        : sessionId(other.sessionId)
        , parent(other.parent)
        , active(false)  // PSYCHOTIC FIX: Initialize to false, then load
        , streamIds{}
        , streamCount(0) {  // PSYCHOTIC FIX: Initialize to 0, then load
        // Copy atomic values AFTER initialization
        active.store(other.active.load(std::memory_order_acquire), std::memory_order_release);
        u32 count = other.streamCount.load(std::memory_order_acquire);
        streamCount.store(count, std::memory_order_release);
        // Copy stream IDs array
        for (u32 i = 0; i < count; ++i) {
            streamIds[i] = other.streamIds[i];
        }
    }
    
    // PSYCHOTIC: Copy assignment for TBB concurrent_vector
    FluentSession& operator=(const FluentSession& other) noexcept {
        if (this != &other) {
            sessionId = other.sessionId;
            parent = other.parent;
            active.store(other.active.load(std::memory_order_acquire), std::memory_order_release);
            u32 count = other.streamCount.load(std::memory_order_acquire);
            for (u32 i = 0; i < count; ++i) {
                streamIds[i] = other.streamIds[i];
            }
            streamCount.store(count, std::memory_order_release);
        }
        return *this;
    }
    
    // Origin: Get session ID
    // Output: Session ID
    SessionId getId() const noexcept { return sessionId; }
    
    // Origin: Check if active
    // Output: Active state
    bool isActive() const noexcept { return active.load(std::memory_order_acquire); }
    
    // Origin: Subscribe to stream
    // Input: streamId - Stream to subscribe
    // Output: Success/failure
    bool subscribeToStream(u32 streamId) noexcept;
    
    // Origin: Unsubscribe from stream
    // Input: streamId - Stream to unsubscribe
    // Output: Success/failure
    bool unsubscribeFromStream(u32 streamId) noexcept;
    
    // Origin: Get stream count
    // Output: Number of subscribed streams
    u32 getStreamCount() const noexcept { return streamCount.load(std::memory_order_acquire); }
};

AARENDOCORE_NAMESPACE_END

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage - from Core_CompilerEnforce.h
ENFORCE_NO_MUTEX(AARendoCoreGLM::FluentAPI);
ENFORCE_NO_MUTEX(AARendoCoreGLM::FluentSession);
ENFORCE_NO_MUTEX(AARendoCoreGLM::FluentConfig);

#endif // AARENDOCORE_CORE_FLUENTAPI_H