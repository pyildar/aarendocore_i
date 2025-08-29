//===--- Core_FluentAPI.cpp - Fluent Interface Implementation -----------===//
//
// COMPILATION LEVEL: 6 (Depends ONLY on Level 5 and below)
// ORIGIN: Implementation of Core_FluentAPI.h
//
// PSYCHOTIC PRECISION: EVERY VARIABLE TRACED TO ITS ORIGIN
//===----------------------------------------------------------------------===//

#include "Core_FluentAPI.h"
#include <algorithm>  // std::find

AARENDOCORE_NAMESPACE_BEGIN

// ==========================================================================
// FLUENT API IMPLEMENTATION
// ==========================================================================

// Origin: Default constructor implementation
FluentAPI::FluentAPI() noexcept 
    : config_{}               // Zero-initialize
    , synchronizer_(nullptr)  // No synchronizer yet
    , sessions_{}            // Empty vector
    , pendingProfiles_{}     // Empty vector
    , onSync_(nullptr)       // No callback
    , onTick_(nullptr)       // No callback
    , onBar_(nullptr)        // No callback
    , onError_(nullptr)      // No callback
    , isBuilt_(false)        // Not built
    , isStarted_(false)      // Not started
{
    // Set defaults with PSYCHOTIC PRECISION
    config_.maxSessions = 10000000;  // 10M sessions capability
    config_.numaNode = -1;           // Any NUMA node
    config_.enableAVX2 = true;       // AVX2 by default
    config_.enableTBB = true;        // TBB by default
    config_.streamBufferSize = 65536; // 64KB buffer
    config_.syncFrequency = 1000.0;  // 1kHz default
    config_.bufferWindowNs = 1000000; // 1ms window
    config_.maxLagNs = 2000000;      // 2ms max lag
    config_.leaderMode = 1;          // Latest timestamp mode
    
    // Reserve for efficiency
    sessions_.reserve(1000);
    pendingProfiles_.reserve(32);
}

// Origin: Destructor implementation
FluentAPI::~FluentAPI() noexcept {
    stop();  // Ensure stopped
    sessions_.clear();  // Clear sessions
    if (synchronizer_) {
        delete synchronizer_;  // PSYCHOTIC: Direct delete for raw pointer
        synchronizer_ = nullptr;
    }
}

// Origin: Set max sessions implementation
FluentAPI& FluentAPI::withMaxSessions(u32 max) noexcept {
    config_.maxSessions = max;
    return *this;
}

// Origin: Set NUMA node implementation
FluentAPI& FluentAPI::onNumaNode(i32 node) noexcept {
    config_.numaNode = node;
    return *this;
}

// Origin: Enable AVX2 implementation
FluentAPI& FluentAPI::withAVX2(bool enable) noexcept {
    config_.enableAVX2 = enable;
    return *this;
}

// Origin: Enable TBB implementation
FluentAPI& FluentAPI::withTBB(bool enable) noexcept {
    config_.enableTBB = enable;
    return *this;
}

// Origin: Set sync frequency implementation
FluentAPI& FluentAPI::atFrequency(f64 freq) noexcept {
    config_.syncFrequency = freq;
    return *this;
}

// Origin: Set buffer window implementation
FluentAPI& FluentAPI::withBufferWindow(u64 ns) noexcept {
    config_.bufferWindowNs = ns;
    config_.maxLagNs = ns * 2;  // Max lag is 2x buffer window
    return *this;
}

// Origin: Add stream implementation
FluentAPI& FluentAPI::addStream(const StreamProfile& profile) noexcept {
    pendingProfiles_.push_back(profile);
    return *this;
}

// Origin: Set sync callback implementation
FluentAPI& FluentAPI::onSynchronized(std::function<void(const SynchronizedOutput&)> callback) noexcept {
    onSync_ = callback;
    return *this;
}

// Origin: Set tick callback implementation
FluentAPI& FluentAPI::onTick(std::function<void(u32, const Tick&)> callback) noexcept {
    onTick_ = callback;
    return *this;
}

// Origin: Set bar callback implementation
FluentAPI& FluentAPI::onBar(std::function<void(u32, const Bar&)> callback) noexcept {
    onBar_ = callback;
    return *this;
}

// Origin: Set error callback implementation
FluentAPI& FluentAPI::onError(std::function<void(const char*)> callback) noexcept {
    onError_ = callback;
    return *this;
}

// Origin: Build system implementation
bool FluentAPI::build() noexcept {
    if (isBuilt_) return true;  // Already built
    
    // Create synchronizer with NUMA affinity - PSYCHOTIC: Direct allocation
    synchronizer_ = new StreamSynchronizer(config_.numaNode);
    
    // Configure synchronizer
    SynchronizerConfig syncConfig{};
    syncConfig.bufferWindowNs = config_.bufferWindowNs;
    syncConfig.maxLagNs = config_.maxLagNs;
    syncConfig.leaderMode = config_.leaderMode;
    syncConfig.enableAVX2 = config_.enableAVX2;
    syncConfig.enableCorrelation = true;
    syncConfig.enableAdaptive = true;
    syncConfig.maxStreams = 32;
    syncConfig.syncFrequency = config_.syncFrequency;
    
    if (!synchronizer_->configure(syncConfig)) {
        if (onError_) onError_("Failed to configure synchronizer");
        return false;
    }
    
    // Add all pending streams
    for (const auto& profile : pendingProfiles_) {
        i32 result = synchronizer_->addStream(profile);
        if (result < 0) {
            if (onError_) onError_("Failed to add stream");
            return false;
        }
    }
    
    isBuilt_ = true;
    return true;
}

// Origin: Start processing implementation
bool FluentAPI::start() noexcept {
    if (!isBuilt_) {
        if (onError_) onError_("Not built");
        return false;
    }
    
    if (isStarted_) return true;  // Already started
    
    isStarted_ = true;
    return true;
}

// Origin: Stop processing implementation
void FluentAPI::stop() noexcept {
    isStarted_ = false;
    
    // Deactivate all sessions - PSYCHOTIC: Direct struct access!
    for (auto& session : sessions_) {
        session.active.store(false, std::memory_order_release);
    }
    
    // Reset synchronizer
    if (synchronizer_) {
        synchronizer_->reset();
    }
}

// Origin: Process tick implementation
bool FluentAPI::processTick(u32 streamId, const Tick& tick) noexcept {
    if (!isStarted_) return false;
    
    // Update stream with tick
    if (synchronizer_ && !synchronizer_->updateStream(streamId, tick)) {
        if (onError_) onError_("Failed to update stream with tick");
        return false;
    }
    
    // Trigger tick callback
    if (onTick_) {
        onTick_(streamId, tick);
    }
    
    return true;
}

// Origin: Process bar implementation
bool FluentAPI::processBar(u32 streamId, const Bar& bar) noexcept {
    if (!isStarted_) return false;
    
    // Update stream with bar
    if (synchronizer_ && !synchronizer_->updateBar(streamId, bar)) {
        if (onError_) onError_("Failed to update stream with bar");
        return false;
    }
    
    // Trigger bar callback
    if (onBar_) {
        onBar_(streamId, bar);
    }
    
    return true;
}

// Origin: Force synchronization implementation
bool FluentAPI::synchronizeNow() noexcept {
    if (!isStarted_ || !synchronizer_) return false;
    
    SynchronizedOutput output{};
    if (!synchronizer_->synchronize(output)) {
        if (onError_) onError_("Failed to synchronize");
        return false;
    }
    
    // Trigger sync callback
    if (onSync_) {
        onSync_(output);
    }
    
    return true;
}

// Origin: Get session count implementation
u32 FluentAPI::getSessionCount() const noexcept {
    return static_cast<u32>(sessions_.size());
}

// Origin: Create session implementation
FluentSession* FluentAPI::createSession() noexcept {
    // Generate unique session ID
    SessionId id = GenerateSessionId(sessions_.size());
    
    // PSYCHOTIC: Create session and add to TBB vector!
    FluentSession newSession(this, id);
    sessions_.push_back(newSession);
    
    // Return pointer to last element
    return &sessions_.back();
}

// Origin: Destroy session implementation
void FluentAPI::destroySession(FluentSession* session) noexcept {
    if (!session) return;
    
    // PSYCHOTIC: Just mark as inactive, NO removal for 10M sessions!
    // TBB concurrent_vector has NO erase - and removal would be O(n)!
    session->active.store(false, std::memory_order_release);
}

// ==========================================================================
// FLUENT SESSION IMPLEMENTATION
// ==========================================================================

// Origin: Constructor implementation - PUBLIC for 10M sessions!
FluentSession::FluentSession(FluentAPI* p, SessionId id) noexcept
    : sessionId(id)
    , parent(p)
    , active(true)
    , streamIds{}
    , streamCount(0)
{
    // PSYCHOTIC: Zero-initialize fixed array
    for (u32 i = 0; i < 32; ++i) {
        streamIds[i] = 0;
    }
}

// Origin: Subscribe to stream implementation
bool FluentSession::subscribeToStream(u32 streamId) noexcept {
    if (!active.load(std::memory_order_acquire)) return false;
    
    // PSYCHOTIC: Lock-free check and add
    u32 count = streamCount.load(std::memory_order_acquire);
    if (count >= 32) return false;  // Full
    
    // Check if already subscribed - PSYCHOTIC: Manual loop!
    for (u32 i = 0; i < count; ++i) {
        if (streamIds[i] == streamId) return true;  // Already subscribed
    }
    
    // Add stream atomically
    streamIds[count] = streamId;
    streamCount.fetch_add(1, std::memory_order_release);
    return true;
}

// Origin: Unsubscribe from stream implementation
bool FluentSession::unsubscribeFromStream(u32 streamId) noexcept {
    if (!active.load(std::memory_order_acquire)) return false;
    
    // PSYCHOTIC: Lock-free removal by shifting
    u32 count = streamCount.load(std::memory_order_acquire);
    bool found = false;
    
    for (u32 i = 0; i < count; ++i) {
        if (streamIds[i] == streamId) {
            found = true;
            // Shift remaining elements
            for (u32 j = i; j < count - 1; ++j) {
                streamIds[j] = streamIds[j + 1];
            }
            streamCount.fetch_sub(1, std::memory_order_release);
            break;
        }
    }
    
    return found;
}

AARENDOCORE_NAMESPACE_END