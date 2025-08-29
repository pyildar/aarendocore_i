//===--- Core_StreamSynchronizer.h - Multi-Stream Synchronization -------===//
//
// COMPILATION LEVEL: 5 (Depends on ProcessingUnits)
// ORIGIN: NEW - Leader-follower stream synchronization
// DEPENDENCIES: Core_Types.h, Core_InterpolationProcessingUnit.h
// DEPENDENTS: None yet
//
// Synchronizes multiple streams with PSYCHOTIC PRECISION.
// Leader detection, gap filling, Renko/Range bar handling.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_STREAMSYNCHRONIZER_H
#define AARENDOCORE_CORE_STREAMSYNCHRONIZER_H

#include "Core_Platform.h"  // For AARENDOCORE_CACHE_LINE_SIZE, AARENDOCORE_ULTRA_PAGE_SIZE
#include "Core_Types.h"
#include "Core_Config.h"
#include "Core_PrimitiveTypes.h"
#include "Core_Session.h"
#include "Core_InterpolationProcessingUnit.h"

// Define shortcuts for constants
#define CACHE_LINE_SIZE AARENDOCORE_CACHE_LINE_SIZE
#define ULTRA_PAGE_SIZE AARENDOCORE_ULTRA_PAGE_SIZE
#include <immintrin.h>

// Enforce compilation level
#ifndef CORE_STREAMSYNCHRONIZER_LEVEL_DEFINED
#define CORE_STREAMSYNCHRONIZER_LEVEL_DEFINED
static constexpr int StreamSynchronizer_CompilationLevel = 5;
#endif

namespace AARendoCoreGLM {

// ==========================================================================
// FILL STRATEGY ENUMERATION
// ==========================================================================

// Origin: Enumeration for fill strategies
// Scope: Stream synchronization decisions
enum class FillStrategy : u8 {
    OLD_TICK = 0,       // Use last available tick
    LAST_BAR = 1,       // Use last completed bar close
    INTERPOLATE = 2,    // Time-based interpolation
    RENKO_FILL = 3,     // Last completed Renko brick
    RANGE_FILL = 4,     // Last completed range bar
    VOLUME_FILL = 5     // Last completed volume bar
};

// ==========================================================================
// BAR TYPE ENUMERATION
// ==========================================================================

// Origin: Enumeration for bar types
// Scope: Stream characterization
enum class BarType : u8 {
    TIME_BASED = 0,     // Regular time intervals
    TICK_BASED = 1,     // Fixed tick count
    RENKO = 2,          // Price movement bricks
    RANGE = 3,          // Price range bars
    VOLUME = 4          // Volume threshold bars
};

// ==========================================================================
// STREAM PROFILE - CONFIGURATION PER STREAM
// ==========================================================================

// Origin: Structure for stream characteristics
// Scope: Per-stream configuration
struct alignas(CACHE_LINE_SIZE) StreamProfile {
    // Origin: Member - Stream identifier, Scope: Profile lifetime
    u32 streamId;
    
    // Origin: Member - Is regular time series, Scope: Profile lifetime
    bool isRegular;
    
    // Origin: Member - Use old tick mode, Scope: Profile lifetime
    bool useOldTick;
    
    // Origin: Member - Use last bar mode, Scope: Profile lifetime
    bool useLastBar;
    
    // Origin: Member - Bar type, Scope: Profile lifetime
    BarType barType;
    
    // Origin: Member - Bar period/size, Scope: Profile lifetime
    u32 barPeriod;
    
    // Origin: Member - Fill strategy, Scope: Profile lifetime
    FillStrategy strategy;
    
    // Origin: Member - NinjaTrader stream index, Scope: Profile lifetime
    u32 ninjaStreamIndex;
    
    // Origin: Member - Instrument identifier, Scope: Profile lifetime
    u32 instrumentId;
    
    // Origin: Member - Priority for leader selection, Scope: Profile lifetime
    u8 priority;
    
    // Padding to cache line
    char padding[38];
};

static_assert(sizeof(StreamProfile) == CACHE_LINE_SIZE,
              "StreamProfile must be exactly one cache line");

// ==========================================================================
// STREAM STATE - RUNTIME STATE PER STREAM
// ==========================================================================

// Origin: Structure for stream runtime state
// Scope: Real-time tracking
struct alignas(CACHE_LINE_SIZE) StreamState {
    // Origin: Member - Latest timestamp, Scope: Real-time
    AtomicU64 latestTimestamp;
    
    // Origin: Member - Last completed bar time, Scope: Real-time
    AtomicU64 lastCompletedBarTime;
    
    // Origin: Member - Last tick data, Scope: Real-time
    Tick lastTick;
    
    // Origin: Member - Last completed bar, Scope: Real-time
    Bar lastCompletedBar;
    
    // Origin: Member - Current fill strategy, Scope: Real-time
    FillStrategy currentStrategy;
    
    // Origin: Member - Is current leader, Scope: Real-time
    bool isLeader;
    
    // Origin: Member - Is synchronized, Scope: Real-time
    bool isSynchronized;
    
    // Origin: Member - Gap detected, Scope: Real-time
    bool hasGap;
    
    // Padding - calculated for exact cache line
    char padding[4];
};

static_assert(sizeof(StreamState) <= AARENDOCORE_CACHE_LINE_SIZE * 2,
              "StreamState must fit in two cache lines");

// ==========================================================================
// SYNCHRONIZATION CONFIG
// ==========================================================================

// Origin: Structure for synchronizer configuration
// Scope: Instance configuration
struct alignas(CACHE_LINE_SIZE) SynchronizerConfig {
    // Origin: Member - Buffer window in nanoseconds, Scope: Config lifetime
    u64 bufferWindowNs;
    
    // Origin: Member - Maximum lag before gap, Scope: Config lifetime
    u64 maxLagNs;
    
    // Origin: Member - Leader detection mode, Scope: Config lifetime
    u32 leaderMode;
    
    // Origin: Member - Enable AVX2 optimization, Scope: Config lifetime
    bool enableAVX2;
    
    // Origin: Member - Enable cross-stream correlation, Scope: Config lifetime
    bool enableCorrelation;
    
    // Origin: Member - Enable adaptive strategies, Scope: Config lifetime
    bool enableAdaptive;
    
    // PSYCHOTIC PRECISION: Compiler padding for u32 alignment
    u8 _padding1;
    
    // Origin: Member - Maximum streams to sync, Scope: Config lifetime
    u32 maxStreams;
    
    // Origin: Member - Sync frequency in Hz, Scope: Config lifetime
    f64 syncFrequency;
    
    // Padding to cache line
    // u64=8, u64=8, u32=4, bool=1, bool=1, bool=1, u8=1, u32=4, f64=8 = 36 bytes
    // CACHE_LINE_SIZE(64) - 36 = 28 bytes padding
    char padding[28];
};

static_assert(sizeof(SynchronizerConfig) == AARENDOCORE_CACHE_LINE_SIZE,
              "SynchronizerConfig must be exactly one cache line");

// ==========================================================================
// SYNCHRONIZED OUTPUT - ALIGNED MULTI-STREAM DATA
// ==========================================================================

// Origin: Structure for synchronized output
// Scope: Output from synchronization
struct alignas(CACHE_LINE_SIZE) SynchronizedOutput {
    // Origin: Member - Synchronization timestamp, Scope: Output lifetime
    u64 syncTimestamp;
    
    // Origin: Member - Leader stream ID, Scope: Output lifetime
    u32 leaderStreamId;
    
    // Origin: Member - Number of streams, Scope: Output lifetime
    u32 streamCount;
    
    // Origin: Member - Synchronized ticks array, Scope: Output lifetime
    Tick syncedTicks[32];  // Max 32 streams
    
    // Origin: Member - Fill methods used, Scope: Output lifetime
    FillStrategy fillMethods[32];
    
    // Origin: Member - Confidence scores, Scope: Output lifetime
    f32 confidence[32];
    
    // Origin: Member - Synchronization quality, Scope: Output lifetime
    f64 syncQuality;
};

// ==========================================================================
// STREAM SYNCHRONIZER - MAIN CLASS
// ==========================================================================

// Origin: Multi-stream synchronizer with leader-follower pattern
class alignas(ULTRA_PAGE_SIZE) StreamSynchronizer final {
public:
    // ======================================================================
    // PUBLIC CONSTANTS
    // ======================================================================
    
    // Origin: Constant - Maximum supported streams, Scope: Compile-time
    static constexpr u32 MAX_STREAMS = 32;
    
    // Origin: Constant - Sync buffer size, Scope: Compile-time
    static constexpr u32 SYNC_BUFFER_SIZE = 4096;
    
    // Origin: Constant - Correlation window, Scope: Compile-time
    static constexpr u32 CORRELATION_WINDOW = 256;

private:
    // ======================================================================
    // MEMBER VARIABLES - PSYCHOTICALLY ALIGNED
    // ======================================================================
    
    // Origin: Member - Synchronizer configuration, Scope: Instance lifetime
    SynchronizerConfig config_;
    
    // Origin: Member - Stream profiles array, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) StreamProfile profiles_[MAX_STREAMS];
    
    // Origin: Member - Stream states array, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) StreamState states_[MAX_STREAMS];
    
    // Origin: Member - Active stream count, Scope: Instance lifetime
    AtomicU32 activeStreams_;
    
    // Origin: Member - Current leader stream, Scope: Real-time
    AtomicU32 currentLeader_;
    
    // Origin: Member - Synchronization buffer, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) SynchronizedOutput* syncBuffer_;
    
    // Origin: Member - Buffer position, Scope: Real-time
    AtomicU32 bufferPos_;
    
    // Origin: Member - Correlation matrix for cross-stream, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) f64* correlationMatrix_;
    
    // Origin: Member - Interpolation unit for time-based filling, Scope: Instance lifetime
    AARendoCore::InterpolationProcessingUnit* interpolator_;
    
    // Origin: Member - NUMA node for allocation, Scope: Instance lifetime
    i32 numaNode_;
    
    // Origin: Member - Synchronization statistics, Scope: Instance lifetime
    struct alignas(CACHE_LINE_SIZE) SyncStats {
        AtomicU64 totalSyncs;
        AtomicU64 leaderChanges;
        AtomicU64 gapsDetected;
        AtomicU64 interpolationsUsed;
        AtomicU64 renkoFillsUsed;
        AtomicF64 avgSyncQuality;
        char padding[16];
    } stats_;
    
    // ======================================================================
    // PRIVATE METHODS - SYNCHRONIZATION ALGORITHMS
    // ======================================================================
    
    // Origin: Detect current leader stream
    // Input: None (uses internal state)
    // Output: Stream ID of leader
    u32 detectLeader() noexcept;
    
    // Origin: Synchronize single stream to leader
    // Input: streamId - Stream to sync, leaderTime - Target time
    // Output: Synchronized tick
    Tick synchronizeStream(u32 streamId, u64 leaderTime) noexcept;
    
    // Origin: Fill using old tick strategy
    // Input: streamId - Stream ID
    // Output: Last available tick
    Tick fillOldTick(u32 streamId) const noexcept;
    
    // Origin: Fill using last bar strategy
    // Input: streamId - Stream ID
    // Output: Tick from last bar close
    Tick fillLastBar(u32 streamId) const noexcept;
    
    // Origin: Fill using Renko brick
    // Input: streamId - Stream ID
    // Output: Tick from last Renko brick
    Tick fillRenko(u32 streamId) const noexcept;
    
    // Origin: Calculate cross-stream correlation
    // Input: stream1, stream2 - Stream IDs
    // Output: Correlation coefficient
    f64 calculateCorrelation(u32 stream1, u32 stream2) noexcept;
    
    // Origin: Adaptive strategy selection
    // Input: streamId - Stream ID, lag - Time lag
    // Output: Best fill strategy
    FillStrategy selectStrategy(u32 streamId, u64 lag) noexcept;
    
    // Origin: AVX2 optimized synchronization
    // Input: streams - Stream array, count - Number
    // Output: Number synchronized
    u32 synchronizeAVX2(const u32* streams, u32 count) noexcept;

public:
    // ======================================================================
    // CONSTRUCTOR/DESTRUCTOR
    // ======================================================================
    
    // Origin: Constructor
    explicit StreamSynchronizer(i32 numaNode = -1) noexcept;
    
    // Origin: Destructor
    ~StreamSynchronizer() noexcept;
    
    // Delete copy/move operations
    StreamSynchronizer(const StreamSynchronizer&) = delete;
    StreamSynchronizer& operator=(const StreamSynchronizer&) = delete;
    StreamSynchronizer(StreamSynchronizer&&) = delete;
    StreamSynchronizer& operator=(StreamSynchronizer&&) = delete;
    
    // ======================================================================
    // PUBLIC INTERFACE
    // ======================================================================
    
    // Origin: Configure synchronizer
    // Input: config - Configuration structure
    // Output: Success/failure
    bool configure(const SynchronizerConfig& config) noexcept;
    
    // Origin: Add stream to synchronizer
    // Input: profile - Stream profile
    // Output: Stream ID or -1 on error
    i32 addStream(const StreamProfile& profile) noexcept;
    
    // Origin: Remove stream from synchronizer
    // Input: streamId - Stream to remove
    // Output: Success/failure
    bool removeStream(u32 streamId) noexcept;
    
    // Origin: Update stream with new tick
    // Input: streamId - Stream ID, tick - New tick data
    // Output: Success/failure
    bool updateStream(u32 streamId, const Tick& tick) noexcept;
    
    // Origin: Update stream with new bar
    // Input: streamId - Stream ID, bar - Completed bar
    // Output: Success/failure
    bool updateBar(u32 streamId, const Bar& bar) noexcept;
    
    // Origin: Synchronize all streams
    // Input: output - Output buffer
    // Output: Success/failure
    bool synchronize(SynchronizedOutput& output) noexcept;
    
    // Origin: Synchronize specific streams
    // Input: streamIds - Array of stream IDs, count - Number
    //        output - Output buffer
    // Output: Number synchronized
    u32 synchronizeStreams(const u32* streamIds, u32 count,
                           SynchronizedOutput& output) noexcept;
    
    // Origin: Get current leader stream
    // Output: Leader stream ID or -1
    i32 getCurrentLeader() const noexcept;
    
    // Origin: Get stream state
    // Input: streamId - Stream ID
    // Output: Pointer to state or nullptr
    const StreamState* getStreamState(u32 streamId) const noexcept;
    
    // Origin: Get synchronization statistics
    // Output: Statistics structure
    void getStatistics(u64& syncs, u64& changes, u64& gaps, f64& quality) const noexcept;
    
    // Origin: Reset synchronizer state
    void reset() noexcept;
    
    // Origin: Force leader stream
    // Input: streamId - Stream to make leader
    // Output: Success/failure
    bool forceLeader(u32 streamId) noexcept;
    
private:
    // Padding to ensure ultra alignment
    char padding_[256];  // Adjust for ULTRA_PAGE_SIZE
};

static_assert(sizeof(StreamSynchronizer) <= AARENDOCORE_ULTRA_PAGE_SIZE * 4,
              "StreamSynchronizer must fit in four ultra pages");

} // namespace AARendoCoreGLM

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage
ENFORCE_NO_MUTEX(AARendoCoreGLM::StreamSynchronizer);
ENFORCE_NO_MUTEX(AARendoCoreGLM::StreamProfile);
ENFORCE_NO_MUTEX(AARendoCoreGLM::StreamState);
ENFORCE_NO_MUTEX(AARendoCoreGLM::SynchronizerConfig);
ENFORCE_NO_MUTEX(AARendoCoreGLM::SynchronizedOutput);

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_StreamSynchronizer);

#endif // AARENDOCORE_CORE_STREAMSYNCHRONIZER_H