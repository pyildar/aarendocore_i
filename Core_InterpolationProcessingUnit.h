//===--- Core_InterpolationProcessingUnit.h - Time-Series Interpolation -===//
//
// COMPILATION LEVEL: 4 (Depends on BaseProcessingUnit)
// ORIGIN: NEW - Interpolation for time-series data
// DEPENDENCIES: Core_BaseProcessingUnit.h, Core_AVX2Math.h
// DEPENDENTS: None
//
// Fills gaps in time-series with PSYCHOTIC precision.
// Multiple interpolation methods with AVX2 optimization.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_INTERPOLATIONPROCESSINGUNIT_H
#define AARENDOCORE_CORE_INTERPOLATIONPROCESSINGUNIT_H

#include "Core_BaseProcessingUnit.h"
#include "Core_AVX2Math.h"
#include "Core_LockFreeQueue.h"
#include "Core_Config.h"
#include <immintrin.h>

// Enforce compilation level
#ifndef CORE_INTERPOLATIONPROCESSINGUNIT_LEVEL_DEFINED
#define CORE_INTERPOLATIONPROCESSINGUNIT_LEVEL_DEFINED
static constexpr int InterpolationProcessingUnit_CompilationLevel = 4;
#endif

namespace AARendoCoreGLM {

// ==========================================================================
// INTERPOLATION METHODS
// ==========================================================================

// Origin: Enumeration for interpolation methods
enum class InterpolationMethod : u8 {
    LINEAR = 0,           // Linear interpolation
    CUBIC_SPLINE = 1,     // Cubic spline (smooth)
    HERMITE = 2,          // Hermite spline (preserves derivatives)
    AKIMA = 3,            // Akima spline (avoids overshooting)
    PCHIP = 4,            // Piecewise Cubic Hermite (monotonic)
    SINC = 5,             // Sinc interpolation (ideal)
    ADAPTIVE = 6          // Adaptive based on data characteristics
};

// ==========================================================================
// INTERPOLATION CONFIGURATION
// ==========================================================================

// Origin: Structure for interpolation configuration
// Scope: Passed during initialization
struct alignas(CACHE_LINE_SIZE) InterpolationConfig {
    // Origin: Member - Interpolation method, Scope: Config lifetime
    InterpolationMethod method;
    
    // Origin: Member - Lookahead points, Scope: Config lifetime
    u32 lookaheadPoints;
    
    // Origin: Member - Lookbehind points, Scope: Config lifetime
    u32 lookbehindPoints;
    
    // Origin: Member - Maximum gap to interpolate (in ticks), Scope: Config lifetime
    u32 maxGapSize;
    
    // Origin: Member - Target sampling rate (Hz), Scope: Config lifetime
    f64 targetSamplingRate;
    
    // Origin: Member - Quality threshold (0-1), Scope: Config lifetime
    f64 qualityThreshold;
    
    // Origin: Member - Enable AVX2 optimization, Scope: Config lifetime
    bool enableAVX2;
    
    // Origin: Member - Enable adaptive method selection, Scope: Config lifetime
    bool enableAdaptive;
    
    // Origin: Member - Enable gap detection, Scope: Config lifetime
    bool enableGapDetection;
    
    // Origin: Member - Enable quality metrics, Scope: Config lifetime
    bool enableQualityMetrics;
    
    // Origin: Member - Number of parallel streams, Scope: Config lifetime
    u32 numStreams;
    
    // Origin: Member - Cross-stream correlation, Scope: Config lifetime
    bool enableCrossStream;
    
    // Padding to cache line
    char padding[7];
};

static_assert(sizeof(InterpolationConfig) == CACHE_LINE_SIZE,
              "InterpolationConfig must be exactly one cache line");

// ==========================================================================
// INTERPOLATION STATISTICS
// ==========================================================================

// Origin: Structure for interpolation statistics
struct alignas(CACHE_LINE_SIZE) InterpolationStatistics {
    // Origin: Member - Total points interpolated, Scope: Session lifetime
    AtomicU64 pointsInterpolated;
    
    // Origin: Member - Total gaps detected, Scope: Session lifetime
    AtomicU64 gapsDetected;
    
    // Origin: Member - Average gap size, Scope: Real-time
    AtomicF64 avgGapSize;
    
    // Origin: Member - Interpolation quality score, Scope: Real-time
    AtomicF64 qualityScore;
    
    // Origin: Member - Min confidence, Scope: Session lifetime
    AtomicF64 minConfidence;
    
    // Origin: Member - Max confidence, Scope: Session lifetime
    AtomicF64 maxConfidence;
    
    // Padding
    char padding[16];
    
    // Default constructor
    InterpolationStatistics() noexcept = default;
    
    // Copy constructor
    InterpolationStatistics(const InterpolationStatistics& other) noexcept {
        pointsInterpolated.store(other.pointsInterpolated.load(std::memory_order_relaxed));
        gapsDetected.store(other.gapsDetected.load(std::memory_order_relaxed));
        avgGapSize.store(other.avgGapSize.load(std::memory_order_relaxed));
        qualityScore.store(other.qualityScore.load(std::memory_order_relaxed));
        minConfidence.store(other.minConfidence.load(std::memory_order_relaxed));
        maxConfidence.store(other.maxConfidence.load(std::memory_order_relaxed));
    }
    
    InterpolationStatistics& operator=(const InterpolationStatistics&) = delete;
};

static_assert(sizeof(InterpolationStatistics) == CACHE_LINE_SIZE,
              "InterpolationStatistics must be exactly one cache line");

// ==========================================================================
// INTERPOLATION POINT - Data with confidence
// ==========================================================================

// Origin: Structure for interpolated point with metadata
struct alignas(32) InterpolatedPoint {
    // Origin: Member - Timestamp, Scope: Point lifetime
    u64 timestamp;
    
    // Origin: Member - Interpolated value, Scope: Point lifetime
    f64 value;
    
    // Origin: Member - Confidence score (0-1), Scope: Point lifetime
    f64 confidence;
    
    // Origin: Member - Method used, Scope: Point lifetime
    InterpolationMethod methodUsed;
    
    // Origin: Member - Is original (not interpolated), Scope: Point lifetime
    bool isOriginal;
    
    // Padding
    char padding[6];
};

// ==========================================================================
// INTERPOLATION PROCESSING UNIT - TIME-SERIES MAGIC
// ==========================================================================

// Origin: Interpolation processing unit with AVX2 optimization
class alignas(ULTRA_PAGE_SIZE) InterpolationProcessingUnit final : public BaseProcessingUnit {
public:
    // ======================================================================
    // PUBLIC CONSTANTS
    // ======================================================================
    
    // Origin: Constant - Maximum buffer size, Scope: Compile-time
    static constexpr u32 MAX_BUFFER_SIZE = 8192;   // 8K points
    
    // Origin: Constant - Maximum streams, Scope: Compile-time
    static constexpr u32 MAX_STREAMS = 32;         // 32 parallel streams
    
    // Origin: Constant - Spline control points, Scope: Compile-time
    static constexpr u32 SPLINE_POINTS = 4;        // For cubic splines

private:
    // ======================================================================
    // MEMBER VARIABLES - PSYCHOTICALLY ALIGNED
    // ======================================================================
    
    // Origin: Member - Interpolation configuration, Scope: Instance lifetime
    InterpolationConfig interpConfig_;
    
    // Origin: Member - Interpolation statistics, Scope: Instance lifetime
    mutable InterpolationStatistics stats_;
    
    // Origin: Member - Circular buffers for each stream, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) InterpolatedPoint* streamBuffers_[MAX_STREAMS];
    
    // Origin: Member - Buffer positions, Scope: Instance lifetime
    AtomicU32 bufferPositions_[MAX_STREAMS];
    
    // Origin: Member - Gap detection thresholds, Scope: Instance lifetime
    f64 gapThresholds_[MAX_STREAMS];
    
    // Origin: Member - Spline coefficients cache, Scope: Instance lifetime
    alignas(32) __m256d splineCoeffs_[SPLINE_POINTS];
    
    // Origin: Member - Quality metrics buffer, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) f64* qualityBuffer_;
    
    // Origin: Member - Last timestamps per stream, Scope: Instance lifetime
    AtomicU64 lastTimestamps_[MAX_STREAMS];
    
    // Origin: Member - Stream correlation matrix, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) f64* correlationMatrix_;
    
    // ======================================================================
    // PRIVATE METHODS - INTERPOLATION ALGORITHMS
    // ======================================================================
    
    // Origin: Linear interpolation between two points
    // Input: p1, p2 - Points, t - Position (0-1)
    // Output: Interpolated value
    f64 linearInterpolate(const InterpolatedPoint& p1, 
                         const InterpolatedPoint& p2, 
                         f64 t) const noexcept;
    
    // Origin: Cubic spline interpolation
    // Input: points - Control points, t - Position
    // Output: Interpolated value
    f64 cubicSplineInterpolate(const InterpolatedPoint* points, 
                               f64 t) const noexcept;
    
    // Origin: Hermite interpolation
    // Input: points - Control points, t - Position
    // Output: Interpolated value
    f64 hermiteInterpolate(const InterpolatedPoint* points, 
                          f64 t) const noexcept;
    
    // Origin: Akima interpolation
    // Input: points - Control points, t - Position
    // Output: Interpolated value
    f64 akimaInterpolate(const InterpolatedPoint* points, 
                        f64 t) const noexcept;
    
    // Origin: Detect gaps in time series
    // Input: streamId - Stream to check
    // Output: Number of gaps detected
    u32 detectGaps(u32 streamId) noexcept;
    
    // Origin: Calculate interpolation quality
    // Input: original - Original points, interpolated - Result
    // Output: Quality score (0-1)
    f64 calculateQuality(const InterpolatedPoint* original,
                        const InterpolatedPoint* interpolated,
                        u32 count) const noexcept;
    
    // Origin: AVX2 optimized interpolation
    // Input: points - Input points, output - Output buffer, count - Number
    // Output: Number interpolated
    u32 interpolateAVX2(const InterpolatedPoint* points,
                       InterpolatedPoint* output,
                       u32 count) noexcept;
    
    // Origin: Cross-stream interpolation
    // Input: streams - Multiple streams, output - Result
    // Output: Number interpolated
    u32 crossStreamInterpolate(const InterpolatedPoint** streams,
                               InterpolatedPoint* output,
                               u32 streamCount, u32 pointCount) noexcept;
    
    // Origin: Adaptive method selection
    // Input: points - Data points, count - Number
    // Output: Best method for this data
    InterpolationMethod selectBestMethod(const InterpolatedPoint* points,
                                        u32 count) const noexcept;

public:
    // ======================================================================
    // CONSTRUCTOR/DESTRUCTOR
    // ======================================================================
    
    // Origin: Constructor
    explicit InterpolationProcessingUnit(i32 numaNode = -1) noexcept;
    
    // Origin: Destructor
    virtual ~InterpolationProcessingUnit() noexcept;
    
    // ======================================================================
    // IPROCESSINGUNIT IMPLEMENTATION
    // ======================================================================
    
    // Origin: Process single tick (adds to interpolation buffer)
    ProcessResult processTick(SessionId sessionId, const Tick& tick) noexcept override;
    
    // Origin: Process batch of ticks
    ProcessResult processBatch(SessionId sessionId, 
                               const Tick* ticks, 
                               usize count) noexcept override;
    
    // Origin: Process stream data
    ProcessResult processStream(SessionId sessionId,
                               const StreamData& streamData) noexcept override;
    
    // ======================================================================
    // INTERPOLATION-SPECIFIC METHODS
    // ======================================================================
    
    // Origin: Configure interpolation
    // Input: config - Interpolation configuration
    // Output: ResultCode
    ResultCode configureInterpolation(const InterpolationConfig& config) noexcept;
    
    // Origin: Interpolate single stream
    // Input: streamId - Stream to interpolate
    //        startTime, endTime - Time range
    //        output - Output buffer
    // Output: Number of points generated
    u32 interpolateStream(u32 streamId, u64 startTime, u64 endTime,
                         InterpolatedPoint* output) noexcept;
    
    // Origin: Interpolate multiple streams
    // Input: streamIds - Array of stream IDs
    //        count - Number of streams
    //        startTime, endTime - Time range
    //        outputs - Output buffers
    // Output: Total points generated
    u32 interpolateMultiStream(const u32* streamIds, u32 count,
                               u64 startTime, u64 endTime,
                               InterpolatedPoint** outputs) noexcept;
    
    // Origin: Get interpolation statistics
    // Output: Current statistics
    InterpolationStatistics getInterpolationStatistics() const noexcept;
    
    // Origin: Reset stream buffer
    // Input: streamId - Stream to reset
    void resetStream(u32 streamId) noexcept;
    
    // Origin: Get confidence for time range
    // Input: streamId - Stream ID
    //        startTime, endTime - Time range
    // Output: Average confidence score
    f64 getConfidence(u32 streamId, u64 startTime, u64 endTime) const noexcept;
    
private:
    // Padding to ensure ultra alignment
    char padding_[512];  // Adjust for ULTRA_PAGE_SIZE
};

static_assert(sizeof(InterpolationProcessingUnit) <= ULTRA_PAGE_SIZE * 2,
              "InterpolationProcessingUnit must fit in two ultra pages");

} // namespace AARendoCoreGLM

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage
ENFORCE_NO_MUTEX(AARendoCoreGLM::InterpolationProcessingUnit);
ENFORCE_NO_MUTEX(AARendoCoreGLM::InterpolationConfig);
ENFORCE_NO_MUTEX(AARendoCoreGLM::InterpolationStatistics);

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_InterpolationProcessingUnit);

#endif // AARENDOCORE_CORE_INTERPOLATIONPROCESSINGUNIT_H