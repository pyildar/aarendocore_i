//===--- Core_BatchProcessingUnit.h - Batch Processing Unit -------------===//
//
// COMPILATION LEVEL: 4 (Depends on BaseProcessingUnit)
// ORIGIN: NEW - Batch processing with SIMD optimization
// DEPENDENCIES: Core_BaseProcessingUnit.h, Core_AVX2Math.h
// DEPENDENTS: None
//
// Processes batches with PSYCHOTIC throughput using AVX2.
// Handles N→1 aggregation and N→K routing.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_BATCHPROCESSINGUNIT_H
#define AARENDOCORE_CORE_BATCHPROCESSINGUNIT_H

#include "Core_BaseProcessingUnit.h"
#include "Core_AVX2Math.h"
#include "Core_LockFreeQueue.h"
#include "Core_Config.h"
#include <immintrin.h>

// Enforce compilation level
#ifndef CORE_BATCHPROCESSINGUNIT_LEVEL_DEFINED
#define CORE_BATCHPROCESSINGUNIT_LEVEL_DEFINED
static constexpr int BatchProcessingUnit_CompilationLevel = 4;
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ==========================================================================
// BATCH PROCESSING MODES
// ==========================================================================

// Origin: Enumeration for batch modes
enum class BatchMode : u8 {
    AGGREGATION = 0,    // N→1 aggregation
    DISTRIBUTION = 1,   // 1→N distribution
    ROUTING = 2,        // N→K routing
    TRANSFORM = 3,      // N→N transformation
    REDUCE = 4,         // Reduce operation
    MAP = 5,            // Map operation
    FILTER = 6          // Filter operation
};

// ==========================================================================
// BATCH PROCESSING CONFIGURATION
// ==========================================================================

// Origin: Structure for batch configuration
// Scope: Passed during initialization
struct alignas(CACHE_LINE_SIZE) BatchProcessingConfig {
    // Origin: Member - Batch mode, Scope: Config lifetime
    BatchMode mode;
    
    // Origin: Member - Input batch size, Scope: Config lifetime
    u32 inputBatchSize;
    
    // Origin: Member - Output batch size, Scope: Config lifetime
    u32 outputBatchSize;
    
    // Origin: Member - Number of input streams, Scope: Config lifetime
    u32 numInputStreams;
    
    // Origin: Member - Number of output streams, Scope: Config lifetime
    u32 numOutputStreams;
    
    // Origin: Member - Enable AVX2 processing, Scope: Config lifetime
    bool enableAVX2;
    
    // Origin: Member - Enable parallel processing, Scope: Config lifetime
    bool enableParallel;
    
    // Origin: Member - Aggregation function ID, Scope: Config lifetime
    u32 aggregationFunction;
    
    // Origin: Member - Transform function ID, Scope: Config lifetime
    u32 transformFunction;
    
    // Origin: Member - Max latency target, Scope: Config lifetime
    u64 maxLatencyNs;
    
    // Padding to cache line
    char padding[11];
};

static_assert(sizeof(BatchProcessingConfig) == CACHE_LINE_SIZE,
              "BatchProcessingConfig must be exactly one cache line");

// ==========================================================================
// BATCH STATISTICS
// ==========================================================================

// Origin: Structure for batch statistics
struct alignas(CACHE_LINE_SIZE) BatchStatistics {
    // Origin: Member - Total batches processed, Scope: Session lifetime
    AtomicU64 batchesProcessed;
    
    // Origin: Member - Total items processed, Scope: Session lifetime
    AtomicU64 itemsProcessed;
    
    // Origin: Member - Average batch size, Scope: Real-time
    AtomicF64 avgBatchSize;
    
    // Origin: Member - Min batch latency, Scope: Session lifetime
    AtomicU64 minLatencyNs;
    
    // Origin: Member - Max batch latency, Scope: Session lifetime
    AtomicU64 maxLatencyNs;
    
    // Origin: Member - Current throughput items/sec, Scope: Real-time
    AtomicF64 throughput;
    
    // Padding
    char padding[8];
    
    // Default constructor
    BatchStatistics() noexcept = default;
    
    // Copy constructor
    BatchStatistics(const BatchStatistics& other) noexcept {
        batchesProcessed.store(other.batchesProcessed.load(std::memory_order_relaxed));
        itemsProcessed.store(other.itemsProcessed.load(std::memory_order_relaxed));
        avgBatchSize.store(other.avgBatchSize.load(std::memory_order_relaxed));
        minLatencyNs.store(other.minLatencyNs.load(std::memory_order_relaxed));
        maxLatencyNs.store(other.maxLatencyNs.load(std::memory_order_relaxed));
        throughput.store(other.throughput.load(std::memory_order_relaxed));
    }
    
    BatchStatistics& operator=(const BatchStatistics&) = delete;
};

static_assert(sizeof(BatchStatistics) == CACHE_LINE_SIZE,
              "BatchStatistics must be exactly one cache line");

// ==========================================================================
// BATCH PROCESSING UNIT - ALIEN LEVEL BATCHING
// ==========================================================================

// Origin: Batch processing unit with SIMD optimization
class alignas(ULTRA_PAGE_SIZE) BatchProcessingUnit final : public BaseProcessingUnit {
public:
    // ======================================================================
    // PUBLIC CONSTANTS
    // ======================================================================
    
    // Origin: Constant - Maximum batch size, Scope: Compile-time
    static constexpr u32 MAX_BATCH_SIZE = 4096;  // 4K items
    
    // Origin: Constant - Maximum streams, Scope: Compile-time
    static constexpr u32 MAX_STREAMS = 64;       // 64 streams
    
    // Origin: Constant - AVX2 batch size, Scope: Compile-time
    static constexpr u32 AVX2_BATCH = 4;         // Process 4 doubles

private:
    // ======================================================================
    // MEMBER VARIABLES - PSYCHOTICALLY ALIGNED
    // ======================================================================
    
    // Origin: Member - Batch configuration, Scope: Instance lifetime
    BatchProcessingConfig batchConfig_;
    
    // Origin: Member - Batch statistics, Scope: Instance lifetime
    mutable BatchStatistics stats_;
    
    // Origin: Member - Input buffers for batching, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) Tick* inputBuffers_[MAX_STREAMS];
    
    // Origin: Member - Output buffers, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) Tick* outputBuffers_[MAX_STREAMS];
    
    // Origin: Member - Current positions in buffers, Scope: Instance lifetime
    AtomicU32 inputPositions_[MAX_STREAMS];
    AtomicU32 outputPositions_[MAX_STREAMS];
    
    // Origin: Member - Batch queue for async processing, Scope: Instance lifetime
    LockFreeQueue<u64, MAX_BATCH_SIZE>* batchQueue_;
    
    // Origin: Member - AVX2 accumulators, Scope: Instance lifetime
    alignas(32) __m256d accumulators_[8];
    
    // Origin: Member - Last batch timestamp, Scope: Instance lifetime
    AtomicU64 lastBatchTime_;
    
    // ======================================================================
    // PRIVATE METHODS - SIMD OPTIMIZED
    // ======================================================================
    
    // Origin: Process batch with AVX2
    // Input: input - Input batch, output - Output batch, count - Items
    // Output: Number processed
    u32 processBatchAVX2(const Tick* input, Tick* output, u32 count) noexcept;
    
    // Origin: Aggregate batch N→1
    // Input: inputs - Multiple input streams, count - Items per stream
    // Output: Aggregated tick
    Tick aggregateBatch(const Tick** inputs, u32 count) noexcept;
    
    // Origin: Route batch N→K
    // Input: input - Input batch, outputs - Output streams, count - Items
    // Output: Number routed
    u32 routeBatch(const Tick* input, Tick** outputs, u32 count) noexcept;
    
    // Origin: Transform batch
    // Input: input - Input batch, output - Output batch, count - Items
    // Output: Number transformed
    u32 transformBatch(const Tick* input, Tick* output, u32 count) noexcept;
    
    // Origin: Apply reduction operation
    // Input: batch - Batch to reduce, count - Items
    // Output: Reduced value
    f64 reduceBatch(const Tick* batch, u32 count) noexcept;
    
    // Origin: Filter batch
    // Input: input - Input batch, output - Output batch, count - Items
    // Output: Number passing filter
    u32 filterBatch(const Tick* input, Tick* output, u32 count) noexcept;

public:
    // ======================================================================
    // CONSTRUCTOR/DESTRUCTOR
    // ======================================================================
    
    // Origin: Constructor
    explicit BatchProcessingUnit(i32 numaNode = -1) noexcept;
    
    // Origin: Destructor
    virtual ~BatchProcessingUnit() noexcept;
    
    // ======================================================================
    // IPROCESSINGUNIT IMPLEMENTATION
    // ======================================================================
    
    // Origin: Process single tick (adds to batch)
    ProcessResult processTick(SessionId sessionId, const Tick& tick) noexcept override;
    
    // Origin: Process batch of ticks with PSYCHOTIC speed
    ProcessResult processBatch(SessionId sessionId, 
                               const Tick* ticks, 
                               usize count) noexcept override;
    
    // Origin: Process stream data
    ProcessResult processStream(SessionId sessionId,
                               const StreamData& streamData) noexcept override;
    
    // ======================================================================
    // BATCH-SPECIFIC METHODS
    // ======================================================================
    
    // Origin: Configure batch processing
    // Input: config - Batch configuration
    // Output: ResultCode
    ResultCode configureBatch(const BatchProcessingConfig& config) noexcept;
    
    // Origin: Execute batch operation
    // Input: mode - Batch mode, inputs - Input streams, outputs - Output streams
    // Output: Number processed
    u32 executeBatch(BatchMode mode, const Tick** inputs, 
                     Tick** outputs, u32 count) noexcept;
    
    // Origin: Get batch statistics
    // Output: Current statistics
    BatchStatistics getBatchStatistics() const noexcept;
    
    // Origin: Flush all pending batches
    // Output: Number of items flushed
    u32 flushAllBatches() noexcept;
    
    // Origin: Reset batch buffers
    void resetBatches() noexcept;
    
private:
    // Padding to ensure ultra alignment
    char padding_[64];  // Reduced for size constraint
};

static_assert(sizeof(BatchProcessingUnit) <= ULTRA_PAGE_SIZE * 4,
              "BatchProcessingUnit must fit in four ultra pages");

AARENDOCORE_NAMESPACE_END

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage
ENFORCE_NO_MUTEX(AARendoCore::BatchProcessingUnit);
ENFORCE_NO_MUTEX(AARendoCore::BatchProcessingConfig);
ENFORCE_NO_MUTEX(AARendoCore::BatchStatistics);

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_BatchProcessingUnit);

#endif // AARENDOCORE_CORE_BATCHPROCESSINGUNIT_H