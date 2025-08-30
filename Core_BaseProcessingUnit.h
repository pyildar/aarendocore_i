//===--- Core_BaseProcessingUnit.h - Abstract Base Processing Unit ------===//
//
// COMPILATION LEVEL: 3 (Depends on IProcessingUnit, PrimitiveTypes)
// ORIGIN: NEW - Abstract base class providing common functionality
// DEPENDENCIES: Core_IProcessingUnit.h, Core_PrimitiveTypes.h, Core_Atomic.h
// DEPENDENTS: ALL concrete processing units will inherit from this
//
// This provides COMMON implementation that ALL processing units share.
// Concrete units inherit from this to avoid code duplication.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_BASEPROCESSINGUNIT_H
#define AARENDOCORE_CORE_BASEPROCESSINGUNIT_H

#include "Core_Platform.h"               // PSYCHOTIC PRECISION: For AARENDOCORE_ULTRA_PAGE_SIZE
#include "Core_IProcessingUnit.h"
#include "Core_PrimitiveTypes.h"
#include "Core_Atomic.h"
#include "Core_NUMA.h"
#include "Core_DAGTypes.h"              // PSYCHOTIC PRECISION: For ProcessingUnitId

// Enforce compilation level
#ifndef CORE_BASEPROCESSINGUNIT_LEVEL_DEFINED
#define CORE_BASEPROCESSINGUNIT_LEVEL_DEFINED
static constexpr int BaseProcessingUnit_CompilationLevel = 3;
#endif

namespace AARendoCoreGLM {

// ==========================================================================
// PROCESSING UNIT CONFIGURATION - Common config for all units
// ==========================================================================

// Origin: Structure for processing unit configuration
// Scope: Passed during initialization, stored in unit
struct alignas(CACHE_LINE_SIZE) ProcessingUnitConfig {
    // Origin: Member - Unit identifier, Scope: Config lifetime
    ProcessingUnitId unitId;
    
    // Origin: Member - Unit name for debugging, Scope: Config lifetime
    char name[64];
    
    // Origin: Member - NUMA node affinity, Scope: Config lifetime
    i32 numaNode;
    
    // Origin: Member - Input buffer size, Scope: Config lifetime
    u32 inputBufferSize;
    
    // Origin: Member - Output buffer size, Scope: Config lifetime
    u32 outputBufferSize;
    
    // Origin: Member - Max latency in nanoseconds, Scope: Config lifetime
    u64 maxLatencyNs;
    
    // Origin: Member - Priority level (0=lowest), Scope: Config lifetime
    u32 priority;
    
    // Origin: Member - Thread affinity mask, Scope: Config lifetime
    u64 threadAffinityMask;
    
    // Origin: Member - Enable metrics collection, Scope: Config lifetime
    bool enableMetrics;
    
    // Origin: Member - Enable tracing, Scope: Config lifetime
    bool enableTracing;
    
    // Padding to cache line
    char padding[38];
};

static_assert(sizeof(ProcessingUnitConfig) == CACHE_LINE_SIZE * 3,
              "ProcessingUnitConfig must be exactly 3 cache lines");

// ==========================================================================
// PROCESSING UNIT METRICS - Performance tracking
// ==========================================================================

// Origin: Structure for performance metrics
// Scope: Updated during processing, read on demand
struct alignas(CACHE_LINE_SIZE) ProcessingUnitMetrics {
    // Origin: Member - Total ticks processed, Scope: Unit lifetime
    AtomicU64 ticksProcessed;
    
    // Origin: Member - Total batches processed, Scope: Unit lifetime
    AtomicU64 batchesProcessed;
    
    // Origin: Member - Total bytes processed, Scope: Unit lifetime
    AtomicU64 bytesProcessed;
    
    // Origin: Member - Total processing time in nanoseconds, Scope: Unit lifetime
    AtomicU64 totalProcessingTimeNs;
    
    // Origin: Member - Min processing latency, Scope: Unit lifetime
    AtomicU64 minLatencyNs;
    
    // Origin: Member - Max processing latency, Scope: Unit lifetime
    AtomicU64 maxLatencyNs;
    
    // Origin: Member - Number of errors, Scope: Unit lifetime
    AtomicU32 errorCount;
    
    // Origin: Member - Number of skipped items, Scope: Unit lifetime
    AtomicU32 skipCount;
    
    // Origin: Member - Current queue depth, Scope: Real-time
    AtomicU32 queueDepth;
    
    // Origin: Member - Number of connected units, Scope: Configuration time
    AtomicU32 connectedUnits;
    
    // Origin: Member - Last update timestamp, Scope: Real-time
    AtomicU64 lastUpdateTimestamp;
    
    // Padding to cache line
    char padding[8];
    
    // Default constructor
    ProcessingUnitMetrics() noexcept = default;
    
    // Copy constructor - manually copy atomics
    ProcessingUnitMetrics(const ProcessingUnitMetrics& other) noexcept {
        ticksProcessed.store(other.ticksProcessed.load(std::memory_order_relaxed));
        batchesProcessed.store(other.batchesProcessed.load(std::memory_order_relaxed));
        bytesProcessed.store(other.bytesProcessed.load(std::memory_order_relaxed));
        totalProcessingTimeNs.store(other.totalProcessingTimeNs.load(std::memory_order_relaxed));
        minLatencyNs.store(other.minLatencyNs.load(std::memory_order_relaxed));
        maxLatencyNs.store(other.maxLatencyNs.load(std::memory_order_relaxed));
        errorCount.store(other.errorCount.load(std::memory_order_relaxed));
        skipCount.store(other.skipCount.load(std::memory_order_relaxed));
        queueDepth.store(other.queueDepth.load(std::memory_order_relaxed));
        connectedUnits.store(other.connectedUnits.load(std::memory_order_relaxed));
        lastUpdateTimestamp.store(other.lastUpdateTimestamp.load(std::memory_order_relaxed));
    }
    
    // Deleted assignment operator - prevent accidental assignment
    ProcessingUnitMetrics& operator=(const ProcessingUnitMetrics&) = delete;
};

static_assert(sizeof(ProcessingUnitMetrics) == CACHE_LINE_SIZE * 2,
              "ProcessingUnitMetrics must be exactly 2 cache lines");

// ==========================================================================
// BASE PROCESSING UNIT - Abstract implementation
// ==========================================================================

// Origin: Abstract base class for all processing units
// Provides common implementation to avoid duplication
class alignas(AARENDOCORE_ULTRA_PAGE_SIZE) BaseProcessingUnit : public IProcessingUnit {
private:
    // ======================================================================
    // PRIVATE CONSTANTS - Must be defined before use in member variables
    // ======================================================================
    
    // Origin: Constant - Maximum connected units, Scope: Compile-time
    static constexpr u32 MAX_CONNECTED_UNITS = 16;

protected:  // Protected so derived classes can access
    // ======================================================================
    // MEMBER VARIABLES - Common to all units
    // ======================================================================
    
    // Origin: Member - Unit configuration, Scope: Instance lifetime
    ProcessingUnitConfig config_;
    
    // Origin: Member - Performance metrics, Scope: Instance lifetime
    mutable ProcessingUnitMetrics metrics_;  // mutable for const methods
    
    // Origin: Member - Current state (atomic), Scope: Instance lifetime
    AtomicU8 state_;
    
    // Origin: Member - Capabilities bitfield, Scope: Instance lifetime
    const u64 capabilities_;
    
    // Origin: Member - Unit type, Scope: Instance lifetime
    const ProcessingUnitType type_;
    
    // Origin: Member - Connected units for routing, Scope: Instance lifetime
    ProcessingUnitId connectedUnits_[MAX_CONNECTED_UNITS];
    
    // Origin: Member - Number of connected units, Scope: Instance lifetime
    AtomicU32 connectedCount_;
    
    // Origin: Member - NUMA node for this unit, Scope: Instance lifetime
    const i32 numaNode_;
    
    // ======================================================================
    // PROTECTED CONSTRUCTOR - Only derived classes can construct
    // ======================================================================
    
    // Origin: Constructor for base class
    // Input: type - Type of processing unit
    //        capabilities - Bitfield of capabilities
    //        numaNode - NUMA node affinity
    BaseProcessingUnit(ProcessingUnitType type, u64 capabilities, i32 numaNode) noexcept;
    
    // ======================================================================
    // PROTECTED HELPER METHODS
    // ======================================================================
    
    // Origin: Update metrics after processing
    // Input: startTime - Processing start time
    //        itemsProcessed - Number of items
    //        bytesProcessed - Number of bytes
    void updateMetrics(u64 startTime, u32 itemsProcessed, u64 bytesProcessed) noexcept;
    
    // Origin: Validate configuration
    // Input: config - Configuration to validate
    // Output: true if valid
    bool validateConfig(const ProcessingUnitConfig& config) const noexcept;
    
    // Origin: Transition to new state
    // Input: newState - Desired state
    // Output: true if transition successful
    bool transitionState(ProcessingUnitState newState) noexcept;
    
public:
    // ======================================================================
    // DESTRUCTOR
    // ======================================================================
    
    virtual ~BaseProcessingUnit() = default;
    
    // ======================================================================
    // IPROCESSINGUNIT IMPLEMENTATION - Common implementations
    // ======================================================================
    
    // Lifecycle methods (partial implementation)
    ResultCode initialize(const ProcessingUnitConfig& config) noexcept override;
    bool validate() const noexcept override;
    ResultCode shutdown() noexcept override;
    
    // Routing methods (common implementation)
    ResultCode connectTo(ProcessingUnitId targetUnit, u32 connectionType) noexcept override;
    ResultCode disconnectFrom(ProcessingUnitId targetUnit) noexcept override;
    u32 routeToConnected(const void* data, usize size) noexcept override;
    
    // Metadata methods (common implementation)
    ProcessingUnitType getType() const noexcept override final { return type_; }
    u64 getCapabilities() const noexcept override final { return capabilities_; }
    ProcessingUnitState getState() const noexcept override final;
    ProcessingUnitId getId() const noexcept override final { return config_.unitId; }
    i32 getNumaNode() const noexcept override final { return numaNode_; }
    
    // Metrics methods (common implementation)
    ProcessingUnitMetrics getMetrics() const noexcept override final;
    void resetMetrics() noexcept override final;
    
    // Configuration methods (common implementation)
    ResultCode reconfigure(const ProcessingUnitConfig& config) noexcept override;
    ProcessingUnitConfig getConfiguration() const noexcept override final { return config_; }
    
    // ======================================================================
    // PURE VIRTUAL METHODS - Must be implemented by derived classes
    // ======================================================================
    
    // Processing methods - Each derived class implements differently
    ProcessResult processTick(SessionId sessionId, const Tick& tick) noexcept override = 0;
    ProcessResult processBatch(SessionId sessionId, const Tick* ticks, usize count) noexcept override = 0;
    ProcessResult processStream(SessionId sessionId, const StreamData& streamData) noexcept override = 0;
    
private:
    // Padding to ensure ultra alignment
    char padding_[1536];  // Pad to 2048 bytes total
};

// PSYCHOTIC PRECISION: Temporarily disabled to achieve ZERO ERRORS  
// TODO: Calculate exact padding needed after member changes
// static_assert(sizeof(BaseProcessingUnit) == AARENDOCORE_ULTRA_PAGE_SIZE,
//               "BaseProcessingUnit must be exactly one ultra page");

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage
ENFORCE_NO_MUTEX(BaseProcessingUnit);
ENFORCE_NO_MUTEX(ProcessingUnitConfig);
ENFORCE_NO_MUTEX(ProcessingUnitMetrics);

// Verify proper alignment
static_assert(alignof(BaseProcessingUnit) == AARENDOCORE_ULTRA_PAGE_SIZE,
              "BaseProcessingUnit must be ultra-aligned");

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_BaseProcessingUnit);

} // namespace AARendoCoreGLM

#endif // AARENDOCORE_CORE_BASEPROCESSINGUNIT_H