//===--- Core_TickProcessingUnit.cpp - Tick Processing Implementation ---===//
//
// COMPILATION LEVEL: 4
// ORIGIN: Implementation for Core_TickProcessingUnit.h
// DEPENDENCIES: Core_TickProcessingUnit.h, Core_AVX2Math.h
// DEPENDENTS: None
//
// Processes market ticks with PSYCHOTIC NANOSECOND precision.
//===----------------------------------------------------------------------===//

#include "Core_TickProcessingUnit.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <malloc.h>

namespace AARendoCoreGLM {

// ==========================================================================
// CONSTRUCTOR/DESTRUCTOR
// ==========================================================================

// Origin: Constructor implementation
TickProcessingUnit::TickProcessingUnit(i32 numaNode) noexcept
    : BaseProcessingUnit(ProcessingUnitType::TICK,
                        CAP_TICK | CAP_BATCH | CAP_STREAM | CAP_SIMD_OPTIMIZED | 
                        CAP_LOCK_FREE | CAP_ZERO_COPY | CAP_REAL_TIME,
                        numaNode)
    , tickConfig_{}
    , stats_{}
    , tickWindow_(nullptr)
    , windowPos_(0)
    , tickQueue_(nullptr)
    , vwapAccumulator_(_mm256_setzero_pd())
    , volumeAccumulator_(_mm256_setzero_pd())
    , lastTimestamp_(0)
    , sessionMultipliers_{1.0, 1.0, 1.0, 1.0}
    , padding_{} {
    
    // Allocate tick window with NUMA awareness
    if (numaNode >= 0) {
        // NUMA-aware allocation would happen here
        tickWindow_ = static_cast<Tick*>(_aligned_malloc(MAX_WINDOW_SIZE * sizeof(Tick),
                                                         CACHE_LINE_SIZE));
    } else {
        tickWindow_ = static_cast<Tick*>(_aligned_malloc(MAX_WINDOW_SIZE * sizeof(Tick),
                                                         CACHE_LINE_SIZE));
    }
    
    // Initialize tick queue
    void* queueMem = _aligned_malloc(sizeof(LockFreeQueue<Tick, MAX_WINDOW_SIZE>), CACHE_LINE_SIZE);
    tickQueue_ = new (queueMem) LockFreeQueue<Tick, MAX_WINDOW_SIZE>();
    
    // Initialize statistics
    stats_.vwap.store(0.0, std::memory_order_relaxed);
    stats_.bid.store(0.0, std::memory_order_relaxed);
    stats_.ask.store(0.0, std::memory_order_relaxed);
    stats_.spread.store(0.0, std::memory_order_relaxed);
    stats_.totalVolume.store(0, std::memory_order_relaxed);
    stats_.windowTickCount.store(0, std::memory_order_relaxed);
    stats_.outlierCount.store(0, std::memory_order_relaxed);
}

// Origin: Destructor implementation
TickProcessingUnit::~TickProcessingUnit() noexcept {
    if (tickWindow_) {
        _aligned_free(tickWindow_);
        tickWindow_ = nullptr;
    }
    
    if (tickQueue_) {
        tickQueue_->~LockFreeQueue();
        _aligned_free(tickQueue_);
        tickQueue_ = nullptr;
    }
}

// ==========================================================================
// TICK PROCESSING - ALIEN LEVEL IMPLEMENTATION
// ==========================================================================

// Origin: Process single tick
// Input: sessionId, tick
// Output: ProcessResult
ProcessResult TickProcessingUnit::processTick([[maybe_unused]] SessionId sessionId, const Tick& tick) noexcept {
    // Validate state
    if (getState() != ProcessingUnitState::READY && 
        getState() != ProcessingUnitState::PROCESSING) {
        return ProcessResult::FAILED;
    }
    
    // Update state to processing
    transitionState(ProcessingUnitState::PROCESSING);
    
    // Check timestamp ordering
    // lastTs: Origin - Local from atomic load, Scope: function
    u64 lastTs = lastTimestamp_.load(std::memory_order_acquire);
    if (tick.timestamp <= lastTs) {
        metrics_.skipCount.fetch_add(1, std::memory_order_relaxed);
        return ProcessResult::SKIP;
    }
    
    // Detect outliers with PSYCHOTIC precision
    if (detectOutlier(tick)) {
        stats_.outlierCount.fetch_add(1, std::memory_order_relaxed);
        metrics_.errorCount.fetch_add(1, std::memory_order_relaxed);
        return ProcessResult::SKIP;
    }
    
    // Process with AVX2 if enabled
    bool processed = tickConfig_.enableAVX2 ? 
                     processTickAVX2(tick) : 
                     processTickStandard(tick);
    
    if (!processed) {
        return ProcessResult::FAILED;
    }
    
    // Update metrics
    metrics_.ticksProcessed.fetch_add(1, std::memory_order_relaxed);
    lastTimestamp_.store(tick.timestamp, std::memory_order_release);
    
    // Update spread if enabled
    if (tickConfig_.enableSpreadTracking) {
        updateSpread(tick);
    }
    
    // Add to window
    // pos: Origin - Local from atomic fetch_add, Scope: function
    u32 pos = windowPos_.fetch_add(1, std::memory_order_acq_rel) % MAX_WINDOW_SIZE;
    tickWindow_[pos] = tick;
    stats_.windowTickCount.fetch_add(1, std::memory_order_relaxed);
    
    return ProcessResult::SUCCESS;
}

// Origin: Process batch of ticks with AVX2
// Input: sessionId, ticks array, count
// Output: ProcessResult
ProcessResult TickProcessingUnit::processBatch([[maybe_unused]] SessionId sessionId, 
                                              const Tick* ticks, 
                                              usize count) noexcept {
    if (!ticks || count == 0) {
        return ProcessResult::FAILED;
    }
    
    // Validate state
    if (getState() != ProcessingUnitState::READY && 
        getState() != ProcessingUnitState::PROCESSING) {
        return ProcessResult::FAILED;
    }
    
    transitionState(ProcessingUnitState::PROCESSING);
    
    // processedCount: Origin - Local counter, Scope: function
    usize processedCount = 0;
    
    // Process in batches of 4 for AVX2 optimization
    if (tickConfig_.enableAVX2 && count >= AVX2_DOUBLES) {
        // Process aligned batches
        for (usize i = 0; i <= count - AVX2_DOUBLES; i += AVX2_DOUBLES) {
            // Prefetch next batch
            if (i + PREFETCH_DISTANCE < count) {
                prefetchTicks(ticks, i + PREFETCH_DISTANCE, count);
            }
            
            // Load 4 prices and volumes into AVX2 registers
            // prices: Origin - Local AVX2 register, Scope: block
            __m256d prices = _mm256_set_pd(
                ticks[i + 3].price,
                ticks[i + 2].price,
                ticks[i + 1].price,
                ticks[i + 0].price
            );
            
            // volumes: Origin - Local AVX2 register, Scope: block
            __m256d volumes = _mm256_set_pd(
                ticks[i + 3].volume,
                ticks[i + 2].volume,
                ticks[i + 1].volume,
                ticks[i + 0].volume
            );
            
            // multipliers: Origin - Local AVX2 register, Scope: block
            __m256d multipliers = _mm256_load_pd(sessionMultipliers_);
            
            // Apply session multipliers
            prices = _mm256_mul_pd(prices, multipliers);
            
            // Update VWAP accumulator
            // priceVolume: Origin - Local AVX2 register, Scope: block
            __m256d priceVolume = _mm256_mul_pd(prices, volumes);
            vwapAccumulator_ = _mm256_add_pd(vwapAccumulator_, priceVolume);
            volumeAccumulator_ = _mm256_add_pd(volumeAccumulator_, volumes);
            
            // Process individual ticks for outlier detection
            for (usize j = 0; j < AVX2_DOUBLES; ++j) {
                if (!detectOutlier(ticks[i + j])) {
                    processedCount++;
                } else {
                    stats_.outlierCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        
        // Process remaining ticks
        for (usize i = (count / AVX2_DOUBLES) * AVX2_DOUBLES; i < count; ++i) {
            if (processTick(sessionId, ticks[i]) == ProcessResult::SUCCESS) {
                processedCount++;
            }
        }
    } else {
        // Standard processing without AVX2
        for (usize i = 0; i < count; ++i) {
            if (processTick(sessionId, ticks[i]) == ProcessResult::SUCCESS) {
                processedCount++;
            }
        }
    }
    
    // Update batch metrics
    metrics_.batchesProcessed.fetch_add(1, std::memory_order_relaxed);
    metrics_.bytesProcessed.fetch_add(count * sizeof(Tick), std::memory_order_relaxed);
    
    return processedCount > 0 ? ProcessResult::SUCCESS : ProcessResult::FAILED;
}

// Origin: Process stream data
ProcessResult TickProcessingUnit::processStream([[maybe_unused]] SessionId sessionId,
                                               const StreamData& streamData) noexcept {
    // Extract ticks from stream data
    if (streamData.dataType != 1) { // Assuming 1 = tick data
        return ProcessResult::FAILED;
    }
    
    // Parse ticks from payload
    // tickCount: Origin - Local from payload parsing, Scope: function
    const usize tickCount = streamData.payload[0];
    const Tick* ticks = reinterpret_cast<const Tick*>(&streamData.payload[1]);
    
    return processBatch(sessionId, ticks, tickCount);
}

// ==========================================================================
// PRIVATE PROCESSING METHODS
// ==========================================================================

// Origin: Process tick with AVX2 optimization
bool TickProcessingUnit::processTickAVX2(const Tick& tick) noexcept {
    // Create AVX2 vector from tick data
    // tickVec: Origin - Local AVX2 register, Scope: function
    __m256d tickVec = _mm256_set_pd(tick.price, tick.volume, 
                                     static_cast<f64>(tick.timestamp), 
                                     static_cast<f64>(tick.flags));
    
    // Apply transformations
    // multiplier: Origin - Local AVX2 register, Scope: function
    __m256d multiplier = _mm256_load_pd(sessionMultipliers_);
    tickVec = _mm256_mul_pd(tickVec, multiplier);
    
    // Update VWAP
    if (tickConfig_.enableVWAP) {
        updateVWAP(tick.price, tick.volume);
    }
    
    return true;
}

// Origin: Process tick with standard math
bool TickProcessingUnit::processTickStandard(const Tick& tick) noexcept {
    // Standard processing without AVX2
    if (tickConfig_.enableVWAP) {
        updateVWAP(tick.price, tick.volume);
    }
    
    // Update statistics
    stats_.totalVolume.fetch_add(static_cast<u64>(tick.volume), 
                                 std::memory_order_relaxed);
    
    return true;
}

// Origin: Update VWAP calculation
void TickProcessingUnit::updateVWAP(f64 price, f64 volume) noexcept {
    // currentVWAP: Origin - Local from atomic load, Scope: function
    f64 currentVWAP = stats_.vwap.load(std::memory_order_relaxed);
    // totalVolume: Origin - Local from atomic load, Scope: function
    u64 totalVolume = stats_.totalVolume.load(std::memory_order_relaxed);
    
    // newVWAP: Origin - Local calculation, Scope: function
    f64 newVWAP = (currentVWAP * totalVolume + price * volume) / 
                  (totalVolume + volume);
    
    stats_.vwap.store(newVWAP, std::memory_order_relaxed);
}

// Origin: Detect outliers with PSYCHOTIC precision
bool TickProcessingUnit::detectOutlier(const Tick& tick) const noexcept {
    // Simple outlier detection based on threshold
    // currentVWAP: Origin - Local from atomic load, Scope: function
    f64 currentVWAP = stats_.vwap.load(std::memory_order_relaxed);
    
    if (currentVWAP > 0.0) {
        // deviation: Origin - Local calculation, Scope: function
        f64 deviation = std::abs(tick.price - currentVWAP) / currentVWAP;
        return deviation > tickConfig_.outlierThreshold;
    }
    
    return false;
}

// Origin: Update spread tracking
void TickProcessingUnit::updateSpread(const Tick& tick) noexcept {
    // Update bid/ask based on flags
    if (tick.flags & 0x01) { // Bid flag
        stats_.bid.store(tick.price, std::memory_order_relaxed);
    }
    if (tick.flags & 0x02) { // Ask flag
        stats_.ask.store(tick.price, std::memory_order_relaxed);
    }
    
    // Calculate spread
    // bid: Origin - Local from atomic load, Scope: function
    f64 bid = stats_.bid.load(std::memory_order_relaxed);
    // ask: Origin - Local from atomic load, Scope: function
    f64 ask = stats_.ask.load(std::memory_order_relaxed);
    
    if (bid > 0.0 && ask > 0.0) {
        stats_.spread.store(ask - bid, std::memory_order_relaxed);
    }
}

// Origin: Prefetch ticks for processing
void TickProcessingUnit::prefetchTicks(const Tick* ticks, usize index, 
                                       usize count) const noexcept {
    if (index < count) {
        _mm_prefetch(reinterpret_cast<const char*>(&ticks[index]), _MM_HINT_T0);
        if (index + 1 < count) {
            _mm_prefetch(reinterpret_cast<const char*>(&ticks[index + 1]), _MM_HINT_T1);
        }
    }
}

// Origin: Aggregate window data
Tick TickProcessingUnit::aggregateWindow() const noexcept {
    // count: Origin - Local from atomic load, Scope: function
    u32 count = stats_.windowTickCount.load(std::memory_order_relaxed);
    if (count == 0) {
        return Tick{0, 0.0, 0.0, 0};
    }
    
    // Calculate aggregated values
    // result: Origin - Local aggregate, Scope: function
    Tick result{};
    // sumPrice: Origin - Local accumulator, Scope: function
    f64 sumPrice = 0.0;
    // sumVolume: Origin - Local accumulator, Scope: function
    f64 sumVolume = 0.0;
    
    // effectiveCount: Origin - Local minimum, Scope: function
    u32 effectiveCount = std::min(count, MAX_WINDOW_SIZE);
    
    for (u32 i = 0; i < effectiveCount; ++i) {
        sumPrice += tickWindow_[i].price * tickWindow_[i].volume;
        sumVolume += tickWindow_[i].volume;
        result.timestamp = std::max(result.timestamp, tickWindow_[i].timestamp);
    }
    
    result.price = sumVolume > 0.0 ? sumPrice / sumVolume : 0.0;
    result.volume = sumVolume;
    result.flags = 0x10; // Aggregated flag
    
    return result;
}

// ==========================================================================
// TICK-SPECIFIC METHODS
// ==========================================================================

// Origin: Configure tick processing parameters
ResultCode TickProcessingUnit::configureTick(const TickProcessingConfig& config) noexcept {
    if (config.windowSize > MAX_WINDOW_SIZE) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    tickConfig_ = config;
    
    // Reset window if size changed
    if (config.windowSize != tickConfig_.windowSize) {
        resetWindow();
    }
    
    return ResultCode::SUCCESS;
}

// Origin: Get current tick statistics
TickStatistics TickProcessingUnit::getTickStatistics() const noexcept {
    // Return copy - copy constructor handles atomics
    return stats_;
}

// Origin: Reset tick window
void TickProcessingUnit::resetWindow() noexcept {
    windowPos_.store(0, std::memory_order_release);
    stats_.windowTickCount.store(0, std::memory_order_release);
    
    // Clear window data
    std::memset(tickWindow_, 0, MAX_WINDOW_SIZE * sizeof(Tick));
}

// Origin: Get aggregated window data
Tick TickProcessingUnit::getWindowAggregate() const noexcept {
    return aggregateWindow();
}

// Origin: Force flush pending ticks
u32 TickProcessingUnit::flushPendingTicks() noexcept {
    // flushed: Origin - Local counter, Scope: function
    u32 flushed = 0;
    
    // Process all queued ticks
    Tick tick;
    while (tickQueue_->dequeue(tick)) {
        processTick(SessionId{0}, tick);
        flushed++;
    }
    
    return flushed;
}

} // namespace AARendoCoreGLM