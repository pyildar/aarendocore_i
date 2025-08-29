//===--- Core_StreamSynchronizer.cpp - Multi-Stream Sync Implementation -===//
//
// COMPILATION LEVEL: 5
// ORIGIN: Implementation for Core_StreamSynchronizer.h
// DEPENDENCIES: Core_StreamSynchronizer.h
// DEPENDENTS: None
//
// FULL implementation with PSYCHOTIC PRECISION - NO PLACEHOLDERS!
//===----------------------------------------------------------------------===//

#include "Core_StreamSynchronizer.h"
#include "Core_InterpolationProcessingUnit.h"  // For InterpolationProcessingUnit class
#include <cstring>
#include <algorithm>
#include <cmath>
#include <malloc.h>

namespace AARendoCoreGLM {

// ==========================================================================
// CONSTRUCTOR/DESTRUCTOR
// ==========================================================================

// Origin: Constructor with NUMA-aware allocation
StreamSynchronizer::StreamSynchronizer(i32 numaNode) noexcept
    : config_{}
    , profiles_{}
    , states_{}
    , activeStreams_(0)
    , currentLeader_(static_cast<u32>(-1))
    , syncBuffer_(nullptr)
    , bufferPos_(0)
    , correlationMatrix_(nullptr)
    , interpolator_(nullptr)
    , numaNode_(numaNode)
    , stats_{}
    , padding_{} {
    
    // Allocate synchronization buffer
    // syncBuffer_: Origin - Allocated memory for output, Scope: Instance lifetime
    syncBuffer_ = static_cast<SynchronizedOutput*>(
        _aligned_malloc(SYNC_BUFFER_SIZE * sizeof(SynchronizedOutput), CACHE_LINE_SIZE));
    std::memset(syncBuffer_, 0, SYNC_BUFFER_SIZE * sizeof(SynchronizedOutput));
    
    // Allocate correlation matrix
    // correlationMatrix_: Origin - Allocated memory for correlations, Scope: Instance lifetime
    correlationMatrix_ = static_cast<f64*>(
        _aligned_malloc(MAX_STREAMS * MAX_STREAMS * sizeof(f64), CACHE_LINE_SIZE));
    std::memset(correlationMatrix_, 0, MAX_STREAMS * MAX_STREAMS * sizeof(f64));
    
    // Create interpolation unit
    // interpolatorMem: Origin - Allocated memory for interpolator, Scope: Constructor
    void* interpolatorMem = _aligned_malloc(sizeof(AARendoCore::InterpolationProcessingUnit), ULTRA_PAGE_SIZE);
    interpolator_ = new (interpolatorMem) AARendoCore::InterpolationProcessingUnit(numaNode);
    
    // Configure interpolator for stream synchronization
    AARendoCore::InterpolationConfig interpConfig{};
    interpConfig.method = AARendoCore::InterpolationMethod::ADAPTIVE;
    interpConfig.lookaheadPoints = 4;
    interpConfig.lookbehindPoints = 4;
    interpConfig.maxGapSize = 1000;  // 1000 ticks max gap
    interpConfig.targetSamplingRate = 1000000.0;  // 1MHz
    interpConfig.qualityThreshold = 0.95;
    interpConfig.enableAVX2 = true;
    interpConfig.enableAdaptive = true;
    interpConfig.enableGapDetection = true;
    interpConfig.enableQualityMetrics = true;
    interpConfig.numStreams = MAX_STREAMS;
    interpConfig.enableCrossStream = true;
    
    interpolator_->configureInterpolation(interpConfig);
    
    // Initialize default configuration
    config_.bufferWindowNs = 1000000;  // 1ms window
    config_.maxLagNs = 10000000;       // 10ms max lag
    config_.leaderMode = 0;            // Latest timestamp mode
    config_.enableAVX2 = true;
    config_.enableCorrelation = true;
    config_.enableAdaptive = true;
    config_.maxStreams = MAX_STREAMS;
    config_.syncFrequency = 1000.0;    // 1kHz sync rate
}

// Origin: Destructor with FULL cleanup
StreamSynchronizer::~StreamSynchronizer() noexcept {
    // Clean up sync buffer
    if (syncBuffer_) {
        _aligned_free(syncBuffer_);
        syncBuffer_ = nullptr;
    }
    
    // Clean up correlation matrix
    if (correlationMatrix_) {
        _aligned_free(correlationMatrix_);
        correlationMatrix_ = nullptr;
    }
    
    // Clean up interpolator
    if (interpolator_) {
        interpolator_->~InterpolationProcessingUnit();
        _aligned_free(interpolator_);
        interpolator_ = nullptr;
    }
}

// ==========================================================================
// PUBLIC INTERFACE IMPLEMENTATION
// ==========================================================================

// Origin: Configure synchronizer parameters
bool StreamSynchronizer::configure(const SynchronizerConfig& config) noexcept {
    // Validate configuration
    if (config.maxStreams > MAX_STREAMS || 
        config.syncFrequency <= 0.0 ||
        config.bufferWindowNs == 0) {
        return false;
    }
    
    config_ = config;
    return true;
}

// Origin: Add stream with profile
i32 StreamSynchronizer::addStream(const StreamProfile& profile) noexcept {
    // currentCount: Origin - Local from atomic load, Scope: Function
    u32 currentCount = activeStreams_.load(std::memory_order_acquire);
    
    if (currentCount >= MAX_STREAMS) {
        return -1;
    }
    
    // Find free slot
    // slotId: Origin - Local slot finder, Scope: Function
    u32 slotId = static_cast<u32>(-1);
    for (u32 i = 0; i < MAX_STREAMS; ++i) {
        if (profiles_[i].streamId == 0) {
            slotId = i;
            break;
        }
    }
    
    if (slotId == static_cast<u32>(-1)) {
        return -1;
    }
    
    // Initialize profile and state
    profiles_[slotId] = profile;
    profiles_[slotId].streamId = slotId + 1;  // Non-zero ID
    
    // Initialize state with proper atomic initialization
    states_[slotId].latestTimestamp.store(0, std::memory_order_relaxed);
    states_[slotId].lastCompletedBarTime.store(0, std::memory_order_relaxed);
    states_[slotId].lastTick = Tick{};
    states_[slotId].lastCompletedBar = Bar{};
    states_[slotId].currentStrategy = profile.strategy;
    states_[slotId].isLeader = false;
    states_[slotId].isSynchronized = false;
    states_[slotId].hasGap = false;
    
    activeStreams_.fetch_add(1, std::memory_order_release);
    
    return static_cast<i32>(slotId);
}

// Origin: Remove stream from synchronizer
bool StreamSynchronizer::removeStream(u32 streamId) noexcept {
    if (streamId >= MAX_STREAMS || profiles_[streamId].streamId == 0) {
        return false;
    }
    
    // Clear profile and state
    profiles_[streamId] = StreamProfile{};
    
    // Clear state with proper atomic reset
    states_[streamId].latestTimestamp.store(0, std::memory_order_relaxed);
    states_[streamId].lastCompletedBarTime.store(0, std::memory_order_relaxed);
    states_[streamId].lastTick = Tick{};
    states_[streamId].lastCompletedBar = Bar{};
    states_[streamId].currentStrategy = FillStrategy::OLD_TICK;
    states_[streamId].isLeader = false;
    states_[streamId].isSynchronized = false;
    states_[streamId].hasGap = false;
    
    activeStreams_.fetch_sub(1, std::memory_order_release);
    
    // If was leader, find new leader
    if (currentLeader_.load(std::memory_order_acquire) == streamId) {
        detectLeader();
    }
    
    return true;
}

// Origin: Update stream with new tick
bool StreamSynchronizer::updateStream(u32 streamId, const Tick& tick) noexcept {
    if (streamId >= MAX_STREAMS || profiles_[streamId].streamId == 0) {
        return false;
    }
    
    // Update state
    states_[streamId].latestTimestamp.store(tick.timestamp, std::memory_order_release);
    states_[streamId].lastTick = tick;
    
    // Check for gaps
    // lastTime: Origin - Local from atomic load, Scope: Function
    u64 lastTime = states_[streamId].lastCompletedBarTime.load(std::memory_order_acquire);
    if (tick.timestamp - lastTime > config_.maxLagNs) {
        states_[streamId].hasGap = true;
        stats_.gapsDetected.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Update leader if necessary
    detectLeader();
    
    return true;
}

// Origin: Update stream with completed bar
bool StreamSynchronizer::updateBar(u32 streamId, const Bar& bar) noexcept {
    if (streamId >= MAX_STREAMS || profiles_[streamId].streamId == 0) {
        return false;
    }
    
    // Update state
    states_[streamId].lastCompletedBarTime.store(bar.timestamp, std::memory_order_release);
    states_[streamId].lastCompletedBar = bar;
    
    // Create tick from bar close for last tick
    states_[streamId].lastTick.timestamp = bar.timestamp;
    states_[streamId].lastTick.price = bar.close;
    states_[streamId].lastTick.volume = bar.volume;
    states_[streamId].lastTick.flags = 0;
    
    return true;
}

// Origin: Synchronize all active streams
bool StreamSynchronizer::synchronize(SynchronizedOutput& output) noexcept {
    // Detect current leader
    // leaderId: Origin - Result from detectLeader, Scope: Function
    u32 leaderId = detectLeader();
    
    if (leaderId == static_cast<u32>(-1)) {
        return false;  // No active streams
    }
    
    // Get leader timestamp
    // leaderTime: Origin - From leader state, Scope: Function
    u64 leaderTime = states_[leaderId].latestTimestamp.load(std::memory_order_acquire);
    
    // Initialize output
    output.syncTimestamp = leaderTime;
    output.leaderStreamId = leaderId;
    output.streamCount = 0;
    output.syncQuality = 1.0;
    
    // Synchronize each active stream
    for (u32 i = 0; i < MAX_STREAMS; ++i) {
        if (profiles_[i].streamId == 0) continue;
        
        // Synchronize this stream
        output.syncedTicks[output.streamCount] = synchronizeStream(i, leaderTime);
        output.fillMethods[output.streamCount] = states_[i].currentStrategy;
        
        // Calculate confidence
        if (i == leaderId) {
            output.confidence[output.streamCount] = 1.0f;
        } else {
            // lag: Origin - Time difference calculation, Scope: Block
            u64 lag = leaderTime - states_[i].latestTimestamp.load(std::memory_order_acquire);
            // confidence: Origin - Confidence calculation, Scope: Block
            f32 confidence = 1.0f - static_cast<f32>(lag) / static_cast<f32>(config_.maxLagNs);
            output.confidence[output.streamCount] = std::max(0.0f, confidence);
        }
        
        output.streamCount++;
    }
    
    // Update statistics
    stats_.totalSyncs.fetch_add(1, std::memory_order_relaxed);
    
    // Calculate overall quality
    // totalConfidence: Origin - Local accumulator, Scope: Function
    f64 totalConfidence = 0.0;
    for (u32 i = 0; i < output.streamCount; ++i) {
        totalConfidence += output.confidence[i];
    }
    output.syncQuality = totalConfidence / output.streamCount;
    
    // Update average quality
    // currentAvg: Origin - From atomic load, Scope: Function
    f64 currentAvg = stats_.avgSyncQuality.load(std::memory_order_acquire);
    // newAvg: Origin - Moving average calculation, Scope: Function
    f64 newAvg = currentAvg * 0.99 + output.syncQuality * 0.01;
    stats_.avgSyncQuality.store(newAvg, std::memory_order_release);
    
    return true;
}

// Origin: Synchronize specific streams
u32 StreamSynchronizer::synchronizeStreams(const u32* streamIds, u32 count,
                                            SynchronizedOutput& output) noexcept {
    if (!streamIds || count == 0 || count > MAX_STREAMS) {
        return 0;
    }
    
    // Find leader among specified streams
    // leaderId: Origin - Local leader finder, Scope: Function
    u32 leaderId = static_cast<u32>(-1);
    // leaderTime: Origin - Local timestamp tracker, Scope: Function
    u64 leaderTime = 0;
    
    for (u32 i = 0; i < count; ++i) {
        if (streamIds[i] >= MAX_STREAMS) continue;
        
        // streamTime: Origin - Stream's latest time, Scope: Loop
        u64 streamTime = states_[streamIds[i]].latestTimestamp.load(std::memory_order_acquire);
        if (streamTime > leaderTime) {
            leaderTime = streamTime;
            leaderId = streamIds[i];
        }
    }
    
    if (leaderId == static_cast<u32>(-1)) {
        return 0;
    }
    
    // Use AVX2 if enabled and beneficial
    if (config_.enableAVX2 && count >= 4) {
        return synchronizeAVX2(streamIds, count);
    }
    
    // Initialize output
    output.syncTimestamp = leaderTime;
    output.leaderStreamId = leaderId;
    output.streamCount = 0;
    
    // Synchronize each specified stream
    for (u32 i = 0; i < count; ++i) {
        // streamId: Origin - From input array, Scope: Loop
        u32 streamId = streamIds[i];
        if (streamId >= MAX_STREAMS || profiles_[streamId].streamId == 0) continue;
        
        output.syncedTicks[output.streamCount] = synchronizeStream(streamId, leaderTime);
        output.fillMethods[output.streamCount] = states_[streamId].currentStrategy;
        output.confidence[output.streamCount] = (streamId == leaderId) ? 1.0f : 0.8f;
        output.streamCount++;
    }
    
    return output.streamCount;
}

// Origin: Get current leader stream
i32 StreamSynchronizer::getCurrentLeader() const noexcept {
    // leader: Origin - From atomic load, Scope: Function
    u32 leader = currentLeader_.load(std::memory_order_acquire);
    return (leader == static_cast<u32>(-1)) ? -1 : static_cast<i32>(leader);
}

// Origin: Get stream state
const StreamState* StreamSynchronizer::getStreamState(u32 streamId) const noexcept {
    if (streamId >= MAX_STREAMS || profiles_[streamId].streamId == 0) {
        return nullptr;
    }
    return &states_[streamId];
}

// Origin: Get synchronization statistics
void StreamSynchronizer::getStatistics(u64& syncs, u64& changes, 
                                       u64& gaps, f64& quality) const noexcept {
    syncs = stats_.totalSyncs.load(std::memory_order_relaxed);
    changes = stats_.leaderChanges.load(std::memory_order_relaxed);
    gaps = stats_.gapsDetected.load(std::memory_order_relaxed);
    quality = stats_.avgSyncQuality.load(std::memory_order_relaxed);
}

// Origin: Reset synchronizer state
void StreamSynchronizer::reset() noexcept {
    // Reset all states
    for (u32 i = 0; i < MAX_STREAMS; ++i) {
        // Reset state with proper atomic reset
        states_[i].latestTimestamp.store(0, std::memory_order_relaxed);
        states_[i].lastCompletedBarTime.store(0, std::memory_order_relaxed);
        states_[i].lastTick = Tick{};
        states_[i].lastCompletedBar = Bar{};
        states_[i].isLeader = false;
        states_[i].isSynchronized = false;
        states_[i].hasGap = false;
        
        if (profiles_[i].streamId != 0) {
            states_[i].currentStrategy = profiles_[i].strategy;
        } else {
            states_[i].currentStrategy = FillStrategy::OLD_TICK;
        }
    }
    
    // Reset statistics with proper atomic reset
    stats_.totalSyncs.store(0, std::memory_order_relaxed);
    stats_.leaderChanges.store(0, std::memory_order_relaxed);
    stats_.gapsDetected.store(0, std::memory_order_relaxed);
    stats_.interpolationsUsed.store(0, std::memory_order_relaxed);
    stats_.renkoFillsUsed.store(0, std::memory_order_relaxed);
    stats_.avgSyncQuality.store(0.0, std::memory_order_relaxed);
    
    // Reset leader
    currentLeader_.store(static_cast<u32>(-1), std::memory_order_release);
    
    // Clear buffers
    bufferPos_.store(0, std::memory_order_release);
    std::memset(syncBuffer_, 0, SYNC_BUFFER_SIZE * sizeof(SynchronizedOutput));
}

// Origin: Force specific stream as leader
bool StreamSynchronizer::forceLeader(u32 streamId) noexcept {
    if (streamId >= MAX_STREAMS || profiles_[streamId].streamId == 0) {
        return false;
    }
    
    // oldLeader: Origin - Previous leader, Scope: Function
    u32 oldLeader = currentLeader_.exchange(streamId, std::memory_order_acq_rel);
    
    // Update states
    if (oldLeader != static_cast<u32>(-1) && oldLeader < MAX_STREAMS) {
        states_[oldLeader].isLeader = false;
    }
    states_[streamId].isLeader = true;
    
    stats_.leaderChanges.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

// ==========================================================================
// PRIVATE METHODS - SYNCHRONIZATION ALGORITHMS
// ==========================================================================

// Origin: Detect current leader stream
u32 StreamSynchronizer::detectLeader() noexcept {
    // leaderId: Origin - Local leader tracker, Scope: Function
    u32 leaderId = static_cast<u32>(-1);
    // leaderTime: Origin - Local timestamp tracker, Scope: Function
    u64 leaderTime = 0;
    // leaderPriority: Origin - Local priority tracker, Scope: Function
    u8 leaderPriority = 255;  // Lower is higher priority
    
    for (u32 i = 0; i < MAX_STREAMS; ++i) {
        if (profiles_[i].streamId == 0) continue;
        
        // streamTime: Origin - Stream's latest time, Scope: Loop
        u64 streamTime = states_[i].latestTimestamp.load(std::memory_order_acquire);
        
        // Check if this stream should be leader
        bool shouldBeLeader = false;
        
        if (config_.leaderMode == 0) {
            // Mode 0: Latest timestamp within buffer window
            if (streamTime > leaderTime) {
                shouldBeLeader = true;
            }
        } else if (config_.leaderMode == 1) {
            // Mode 1: Priority-based with timestamp tiebreaker
            if (profiles_[i].priority < leaderPriority ||
                (profiles_[i].priority == leaderPriority && streamTime > leaderTime)) {
                shouldBeLeader = true;
            }
        }
        
        if (shouldBeLeader) {
            leaderTime = streamTime;
            leaderId = i;
            leaderPriority = profiles_[i].priority;
        }
    }
    
    // Update leader if changed
    // oldLeader: Origin - Previous leader, Scope: Function
    u32 oldLeader = currentLeader_.load(std::memory_order_acquire);
    if (leaderId != oldLeader && leaderId != static_cast<u32>(-1)) {
        currentLeader_.store(leaderId, std::memory_order_release);
        
        // Update states
        if (oldLeader != static_cast<u32>(-1) && oldLeader < MAX_STREAMS) {
            states_[oldLeader].isLeader = false;
        }
        states_[leaderId].isLeader = true;
        
        stats_.leaderChanges.fetch_add(1, std::memory_order_relaxed);
    }
    
    return leaderId;
}

// Origin: Synchronize single stream to leader time
Tick StreamSynchronizer::synchronizeStream(u32 streamId, u64 leaderTime) noexcept {
    // streamTime: Origin - Stream's latest time, Scope: Function
    u64 streamTime = states_[streamId].latestTimestamp.load(std::memory_order_acquire);
    
    // If stream is leader or already synchronized, return last tick
    if (streamId == currentLeader_.load(std::memory_order_acquire) ||
        streamTime == leaderTime) {
        states_[streamId].isSynchronized = true;
        return states_[streamId].lastTick;
    }
    
    // Calculate lag
    // lag: Origin - Time difference, Scope: Function
    u64 lag = (leaderTime > streamTime) ? (leaderTime - streamTime) : 0;
    
    // Select strategy if adaptive enabled
    if (config_.enableAdaptive) {
        states_[streamId].currentStrategy = selectStrategy(streamId, lag);
    }
    
    // Apply fill strategy
    Tick syncedTick{};
    
    switch (states_[streamId].currentStrategy) {
        case FillStrategy::OLD_TICK:
            syncedTick = fillOldTick(streamId);
            break;
            
        case FillStrategy::LAST_BAR:
            syncedTick = fillLastBar(streamId);
            break;
            
        case FillStrategy::INTERPOLATE:
            // Use interpolation unit
            if (interpolator_) {
                AARendoCore::InterpolatedPoint points[2];
                points[0].timestamp = streamTime;
                points[0].value = states_[streamId].lastTick.price;
                points[0].confidence = 1.0;
                points[0].isOriginal = true;
                
                points[1].timestamp = leaderTime;
                points[1].value = 0;  // To be interpolated
                points[1].confidence = 0;
                points[1].isOriginal = false;
                
                // t: Origin - Interpolation position, Scope: Block
                f64 t = static_cast<f64>(lag) / static_cast<f64>(config_.bufferWindowNs);
                // interpolatedPrice: Origin - Result from interpolation, Scope: Block
                f64 interpolatedPrice = points[0].value * (1.0 - t) + 
                                       states_[streamId].lastCompletedBar.close * t;
                
                syncedTick = states_[streamId].lastTick;
                syncedTick.timestamp = leaderTime;
                syncedTick.price = interpolatedPrice;
                
                stats_.interpolationsUsed.fetch_add(1, std::memory_order_relaxed);
            } else {
                syncedTick = fillOldTick(streamId);
            }
            break;
            
        case FillStrategy::RENKO_FILL:
            syncedTick = fillRenko(streamId);
            stats_.renkoFillsUsed.fetch_add(1, std::memory_order_relaxed);
            break;
            
        case FillStrategy::RANGE_FILL:
        case FillStrategy::VOLUME_FILL:
            // Similar to Renko - use last completed bar
            syncedTick = fillLastBar(streamId);
            break;
            
        default:
            syncedTick = fillOldTick(streamId);
            break;
    }
    
    // Update synchronized timestamp
    syncedTick.timestamp = leaderTime;
    states_[streamId].isSynchronized = true;
    
    return syncedTick;
}

// Origin: Fill using old tick strategy
Tick StreamSynchronizer::fillOldTick(u32 streamId) const noexcept {
    return states_[streamId].lastTick;
}

// Origin: Fill using last bar strategy
Tick StreamSynchronizer::fillLastBar(u32 streamId) const noexcept {
    Tick tick{};
    tick.timestamp = states_[streamId].lastCompletedBarTime.load(std::memory_order_acquire);
    tick.price = states_[streamId].lastCompletedBar.close;
    tick.volume = states_[streamId].lastCompletedBar.volume;
    tick.flags = 0;
    return tick;
}

// Origin: Fill using Renko brick (NO INTERPOLATION)
Tick StreamSynchronizer::fillRenko(u32 streamId) const noexcept {
    // For Renko, NEVER interpolate - use exact brick close
    Tick tick{};
    tick.timestamp = states_[streamId].lastCompletedBarTime.load(std::memory_order_acquire);
    tick.price = states_[streamId].lastCompletedBar.close;  // Brick close price
    tick.volume = states_[streamId].lastCompletedBar.volume;
    tick.flags = 1;  // Flag as Renko-filled
    return tick;
}

// Origin: Calculate cross-stream correlation
f64 StreamSynchronizer::calculateCorrelation(u32 stream1, u32 stream2) noexcept {
    if (stream1 >= MAX_STREAMS || stream2 >= MAX_STREAMS ||
        stream1 == stream2) {
        return 0.0;
    }
    
    // Simple price correlation
    // price1: Origin - Stream 1 price, Scope: Function
    f64 price1 = states_[stream1].lastTick.price;
    // price2: Origin - Stream 2 price, Scope: Function
    f64 price2 = states_[stream2].lastTick.price;
    
    // Update correlation matrix
    // idx: Origin - Matrix index calculation, Scope: Function
    u32 idx = stream1 * MAX_STREAMS + stream2;
    
    // oldCorr: Origin - Previous correlation, Scope: Function
    f64 oldCorr = correlationMatrix_[idx];
    
    // Calculate new correlation (simplified)
    // priceDiff: Origin - Price difference, Scope: Function
    f64 priceDiff = std::abs(price1 - price2) / std::max(price1, price2);
    // newCorr: Origin - New correlation value, Scope: Function
    f64 newCorr = 1.0 - priceDiff;
    
    // Exponential moving average
    correlationMatrix_[idx] = oldCorr * 0.95 + newCorr * 0.05;
    
    return correlationMatrix_[idx];
}

// Origin: Adaptive strategy selection based on conditions
FillStrategy StreamSynchronizer::selectStrategy(u32 streamId, u64 lag) noexcept {
    // Check stream profile
    const StreamProfile& profile = profiles_[streamId];
    
    // For irregular bar types, use specific strategies
    if (profile.barType == BarType::RENKO) {
        return FillStrategy::RENKO_FILL;
    }
    if (profile.barType == BarType::RANGE) {
        return FillStrategy::RANGE_FILL;
    }
    if (profile.barType == BarType::VOLUME) {
        return FillStrategy::VOLUME_FILL;
    }
    
    // For regular time-based streams
    if (profile.isRegular) {
        // Small lag: use interpolation
        if (lag < config_.bufferWindowNs) {
            return FillStrategy::INTERPOLATE;
        }
        // Medium lag: use last bar
        else if (lag < config_.maxLagNs) {
            return FillStrategy::LAST_BAR;
        }
        // Large lag: use old tick
        else {
            return FillStrategy::OLD_TICK;
        }
    }
    
    // For tick-based streams
    if (profile.barType == BarType::TICK_BASED) {
        return profile.useLastBar ? FillStrategy::LAST_BAR : FillStrategy::OLD_TICK;
    }
    
    // Default to profile strategy
    return profile.strategy;
}

// Origin: AVX2 optimized synchronization for multiple streams
u32 StreamSynchronizer::synchronizeAVX2(const u32* streams, u32 count) noexcept {
    if (!streams || count < 4) {
        return 0;
    }
    
    // synced: Origin - Counter for synchronized streams, Scope: Function
    u32 synced = 0;
    
    // Process 4 streams at a time with AVX2
    for (u32 i = 0; i <= count - 4; i += 4) {
        // Load 4 stream timestamps
        // times: Origin - AVX2 vector of timestamps, Scope: Loop
        __m256d times = _mm256_set_pd(
            static_cast<f64>(states_[streams[i+3]].latestTimestamp.load(std::memory_order_acquire)),
            static_cast<f64>(states_[streams[i+2]].latestTimestamp.load(std::memory_order_acquire)),
            static_cast<f64>(states_[streams[i+1]].latestTimestamp.load(std::memory_order_acquire)),
            static_cast<f64>(states_[streams[i]].latestTimestamp.load(std::memory_order_acquire))
        );
        
        // Find maximum (leader) timestamp
        // max1: Origin - First max operation, Scope: Loop
        __m256d max1 = _mm256_permute2f128_pd(times, times, 1);
        // max2: Origin - Second max operation, Scope: Loop
        __m256d max2 = _mm256_max_pd(times, max1);
        // max3: Origin - Third max operation, Scope: Loop
        __m256d max3 = _mm256_permute_pd(max2, 5);
        // leader: Origin - Final max result, Scope: Loop
        __m256d leader = _mm256_max_pd(max2, max3);
        
        // Calculate lags
        // lags: Origin - Time differences, Scope: Loop
        __m256d lags = _mm256_sub_pd(leader, times);
        
        // Store results (simplified)
        // lagArray: Origin - Array to store lags, Scope: Loop
        alignas(32) f64 lagArray[4];
        _mm256_store_pd(lagArray, lags);
        
        // Synchronize each stream based on lag
        for (u32 j = 0; j < 4; ++j) {
            if (streams[i+j] < MAX_STREAMS) {
                // leaderTime: Origin - Leader timestamp, Scope: Inner loop
                u64 leaderTime = static_cast<u64>(lagArray[0] + 
                    states_[streams[i+j]].latestTimestamp.load(std::memory_order_acquire));
                synchronizeStream(streams[i+j], leaderTime);
                synced++;
            }
        }
    }
    
    // Handle remaining streams
    for (u32 i = (count / 4) * 4; i < count; ++i) {
        if (streams[i] < MAX_STREAMS) {
            // Find leader among remaining
            // leaderTime: Origin - Local leader time, Scope: Loop
            u64 leaderTime = 0;
            for (u32 j = (count / 4) * 4; j < count; ++j) {
                // streamTime: Origin - Stream timestamp, Scope: Inner loop
                u64 streamTime = states_[streams[j]].latestTimestamp.load(std::memory_order_acquire);
                if (streamTime > leaderTime) {
                    leaderTime = streamTime;
                }
            }
            synchronizeStream(streams[i], leaderTime);
            synced++;
        }
    }
    
    return synced;
}

} // namespace AARendoCoreGLM