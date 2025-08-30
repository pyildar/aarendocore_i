//===--- Core_TickProcessingUnit.h - Tick Processing Unit ---------------===//
//
// COMPILATION LEVEL: 4 (Depends on BaseProcessingUnit)
// ORIGIN: NEW - Concrete tick processing implementation
// DEPENDENCIES: Core_BaseProcessingUnit.h, Core_AVX2Math.h
// DEPENDENTS: None
//
// Processes market ticks with PSYCHOTIC NANOSECOND precision.
// ZERO locks, ALIEN LEVEL performance for 10M concurrent sessions.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_TICKPROCESSINGUNIT_H
#define AARENDOCORE_CORE_TICKPROCESSINGUNIT_H

#include "Core_BaseProcessingUnit.h"
#include "Core_AVX2Math.h"
#include "Core_LockFreeQueue.h"
#include "Core_Config.h"
#include <immintrin.h>

// Enforce compilation level
#ifndef CORE_TICKPROCESSINGUNIT_LEVEL_DEFINED
#define CORE_TICKPROCESSINGUNIT_LEVEL_DEFINED
static constexpr int TickProcessingUnit_CompilationLevel = 4;
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ==========================================================================
// TICK PROCESSING CONFIGURATION
// ==========================================================================

// Origin: Structure for tick-specific configuration
// Scope: Passed during initialization
struct alignas(CACHE_LINE_SIZE) TickProcessingConfig {
    // Origin: Member - Tick window size for aggregation, Scope: Config lifetime
    u32 windowSize;
    
    // Origin: Member - Decimation factor for sampling, Scope: Config lifetime
    u32 decimationFactor;
    
    // Origin: Member - Enable VWAP calculation, Scope: Config lifetime
    bool enableVWAP;
    
    // Origin: Member - Enable spread tracking, Scope: Config lifetime
    bool enableSpreadTracking;
    
    // Origin: Member - Max ticks per batch, Scope: Config lifetime
    u32 maxTicksPerBatch;
    
    // Origin: Member - Outlier detection threshold, Scope: Config lifetime
    f64 outlierThreshold;
    
    // Origin: Member - Enable AVX2 optimizations, Scope: Config lifetime
    bool enableAVX2;
    
    // Padding to cache line
    char padding[13];
};

static_assert(sizeof(TickProcessingConfig) == CACHE_LINE_SIZE,
              "TickProcessingConfig must be exactly one cache line");

// ==========================================================================
// TICK STATISTICS - Real-time metrics with PSYCHOTIC precision
// ==========================================================================

// Origin: Structure for tick statistics
// Scope: Updated in real-time during processing
struct alignas(CACHE_LINE_SIZE) TickStatistics {
    // Origin: Member - Volume-weighted average price, Scope: Real-time
    AtomicF64 vwap;
    
    // Origin: Member - Current bid price, Scope: Real-time
    AtomicF64 bid;
    
    // Origin: Member - Current ask price, Scope: Real-time
    AtomicF64 ask;
    
    // Origin: Member - Bid-ask spread, Scope: Real-time
    AtomicF64 spread;
    
    // Origin: Member - Total volume processed, Scope: Session lifetime
    AtomicU64 totalVolume;
    
    // Origin: Member - Number of ticks in current window, Scope: Window lifetime
    AtomicU32 windowTickCount;
    
    // Origin: Member - Number of outliers detected, Scope: Session lifetime
    AtomicU32 outlierCount;
    
    // Padding to cache line
    char padding[4];
    
    // Default constructor
    TickStatistics() noexcept = default;
    
    // Copy constructor - manually copy atomics
    TickStatistics(const TickStatistics& other) noexcept {
        vwap.store(other.vwap.load(std::memory_order_relaxed));
        bid.store(other.bid.load(std::memory_order_relaxed));
        ask.store(other.ask.load(std::memory_order_relaxed));
        spread.store(other.spread.load(std::memory_order_relaxed));
        totalVolume.store(other.totalVolume.load(std::memory_order_relaxed));
        windowTickCount.store(other.windowTickCount.load(std::memory_order_relaxed));
        outlierCount.store(other.outlierCount.load(std::memory_order_relaxed));
    }
    
    // Deleted assignment to prevent accidental misuse
    TickStatistics& operator=(const TickStatistics&) = delete;
};

static_assert(sizeof(TickStatistics) == CACHE_LINE_SIZE,
              "TickStatistics must be exactly one cache line");

// ==========================================================================
// TICK PROCESSING UNIT - ALIEN LEVEL IMPLEMENTATION
// ==========================================================================

// Origin: Concrete implementation for tick processing
// Processes market ticks with NANOSECOND precision
class alignas(ULTRA_PAGE_SIZE) TickProcessingUnit final : public BaseProcessingUnit {
public:
    // ======================================================================
    // PUBLIC CONSTANTS - Must be defined before use in member variables
    // ======================================================================
    
    // Origin: Constant - Maximum window size for aggregation, Scope: Compile-time
    static constexpr u32 MAX_WINDOW_SIZE = 16384;  // 2^14 for lock-free operation
    
private:
    
    // Origin: Constant - AVX2 vector size for f64, Scope: Compile-time
    static constexpr u32 AVX2_DOUBLES = 4;
    
    // Origin: Constant - Cache prefetch distance, Scope: Compile-time
    static constexpr u32 PREFETCH_DISTANCE = 8;

    // ======================================================================
    // MEMBER VARIABLES - PSYCHOTICALLY ALIGNED
    // ======================================================================
    
    // Origin: Member - Tick-specific configuration, Scope: Instance lifetime
    TickProcessingConfig tickConfig_;
    
    // Origin: Member - Real-time statistics, Scope: Instance lifetime
    mutable TickStatistics stats_;
    
    // Origin: Member - Circular buffer for tick window, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) Tick* tickWindow_;
    
    // Origin: Member - Current window position, Scope: Instance lifetime
    AtomicU32 windowPos_;
    
    // Origin: Member - Lock-free queue for batch processing, Scope: Instance lifetime
    LockFreeQueue<Tick, MAX_WINDOW_SIZE>* tickQueue_;
    
    // Origin: Member - AVX2 accumulator for VWAP, Scope: Instance lifetime
    alignas(32) __m256d vwapAccumulator_;
    
    // Origin: Member - AVX2 volume accumulator, Scope: Instance lifetime
    alignas(32) __m256d volumeAccumulator_;
    
    // Origin: Member - Last processed timestamp, Scope: Instance lifetime
    AtomicU64 lastTimestamp_;
    
    // Origin: Member - Session-specific multipliers, Scope: Instance lifetime
    alignas(32) f64 sessionMultipliers_[4];
    
    // ======================================================================
    // PRIVATE METHODS - PSYCHOTIC OPTIMIZATION
    // ======================================================================
    
    // Origin: Process tick with AVX2 optimization
    // Input: tick - Market tick to process
    // Output: true if processed successfully
    bool processTickAVX2(const Tick& tick) noexcept;
    
    // Origin: Process tick with standard math
    // Input: tick - Market tick to process
    // Output: true if processed successfully
    bool processTickStandard(const Tick& tick) noexcept;
    
    // Origin: Update VWAP calculation
    // Input: price - Current price
    //        volume - Current volume
    void updateVWAP(f64 price, f64 volume) noexcept;
    
    // Origin: Detect outliers with PSYCHOTIC precision
    // Input: tick - Tick to check
    // Output: true if outlier detected
    bool detectOutlier(const Tick& tick) const noexcept;
    
    // Origin: Update spread tracking
    // Input: tick - Current tick
    void updateSpread(const Tick& tick) noexcept;
    
    // Origin: Aggregate window data
    // Output: Aggregated tick representing window
    Tick aggregateWindow() const noexcept;
    
    // Origin: Prefetch next ticks for processing
    // Input: ticks - Array of ticks
    //        index - Current index
    //        count - Total count
    void prefetchTicks(const Tick* ticks, usize index, usize count) const noexcept;

public:
    // ======================================================================
    // CONSTRUCTOR/DESTRUCTOR
    // ======================================================================
    
    // Origin: Constructor
    // Creates tick processing unit with PSYCHOTIC precision
    explicit TickProcessingUnit(i32 numaNode = -1) noexcept;
    
    // Origin: Destructor
    virtual ~TickProcessingUnit() noexcept;
    
    // ======================================================================
    // IPROCESSINGUNIT IMPLEMENTATION - ALIEN LEVEL
    // ======================================================================
    
    // Origin: Process single tick with NANOSECOND precision
    // Input: sessionId - Session identifier
    //        tick - Market tick data
    // Output: ProcessResult
    ProcessResult processTick(SessionId sessionId, const Tick& tick) noexcept override;
    
    // Origin: Process batch of ticks with AVX2 optimization
    // Input: sessionId - Session identifier
    //        ticks - Array of ticks
    //        count - Number of ticks
    // Output: ProcessResult
    ProcessResult processBatch(SessionId sessionId, 
                               const Tick* ticks, 
                               usize count) noexcept override;
    
    // Origin: Process stream data
    // Input: sessionId - Session identifier
    //        streamData - Stream packet
    // Output: ProcessResult
    ProcessResult processStream(SessionId sessionId,
                               const StreamData& streamData) noexcept override;
    
    // ======================================================================
    // TICK-SPECIFIC METHODS
    // ======================================================================
    
    // Origin: Configure tick processing parameters
    // Input: config - Tick-specific configuration
    // Output: ResultCode
    ResultCode configureTick(const TickProcessingConfig& config) noexcept;
    
    // Origin: Get current tick statistics
    // Output: Current statistics
    TickStatistics getTickStatistics() const noexcept;
    
    // Origin: Reset tick window
    void resetWindow() noexcept;
    
    // Origin: Get aggregated window data
    // Output: Aggregated tick
    Tick getWindowAggregate() const noexcept;
    
    // Origin: Force flush pending ticks
    // Output: Number of ticks flushed
    u32 flushPendingTicks() noexcept;
    
private:
    // Padding to ensure ultra alignment
    char padding_[512];  // Adjust for exact ULTRA_PAGE_SIZE
};

static_assert(sizeof(TickProcessingUnit) <= ULTRA_PAGE_SIZE * 2,
              "TickProcessingUnit must fit in two ultra pages");

AARENDOCORE_NAMESPACE_END

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage
ENFORCE_NO_MUTEX(AARendoCore::TickProcessingUnit);
ENFORCE_NO_MUTEX(AARendoCore::TickProcessingConfig);
ENFORCE_NO_MUTEX(AARendoCore::TickStatistics);

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_TickProcessingUnit);

#endif // AARENDOCORE_CORE_TICKPROCESSINGUNIT_H