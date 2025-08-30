//===--- Core_IProcessingUnit.h - Processing Unit Interface -------------===//
//
// COMPILATION LEVEL: 2 (Depends on PrimitiveTypes, CompilerEnforce)
// ORIGIN: NEW - Pure interface for ALL processing units
// DEPENDENCIES: Core_PrimitiveTypes.h, Core_CompilerEnforce.h
// DEPENDENTS: ALL processing unit implementations will depend on this
//
// This is the PURE VIRTUAL interface that ALL processing units MUST implement.
// ZERO implementation here - pure abstraction for PSYCHOTIC ISOLATION.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_IPROCESSINGUNIT_H
#define AARENDOCORE_CORE_IPROCESSINGUNIT_H

#include "Core_PrimitiveTypes.h"
#include "Core_CompilerEnforce.h"

// Enforce compilation level  
#ifndef CORE_IPROCESSINGUNIT_LEVEL_DEFINED
#define CORE_IPROCESSINGUNIT_LEVEL_DEFINED
static constexpr int IProcessingUnit_CompilationLevel = 2;
#endif

AARENDOCORE_NAMESPACE_BEGIN

// Forward declarations - Origin: Interface dependencies
// These will be defined in Core_Types.h but we forward declare to avoid circular deps
struct Tick {
    u64 timestamp;
    f64 price;
    f64 volume;
    u32 flags;
};

struct Order {
    u64 orderId;
    u32 type;
    f64 price;
    f64 quantity;
};

struct StreamData {
    u32 streamId;
    u32 dataType;
    u64 timestamp;
    u8 payload[256];
};

// These are defined in Core_BaseProcessingUnit.h
struct ProcessingUnitConfig;
struct ProcessingUnitMetrics;

// ==========================================================================
// PROCESSING UNIT CAPABILITIES - Bitfield for compile-time checks
// ==========================================================================

// Origin: Enumeration for capability flags
// Scope: Global constants for capability checking
enum ProcessingUnitCapability : u64 {
    CAP_NONE           = 0x0000,
    CAP_TICK           = 0x0001,  // Can process ticks
    CAP_ORDER          = 0x0002,  // Can process orders
    CAP_BATCH          = 0x0004,  // Can process batches
    CAP_STREAM         = 0x0008,  // Can process streams
    CAP_INTERPOLATION  = 0x0010,  // Can interpolate data
    CAP_AGGREGATION    = 0x0020,  // Can aggregate data
    CAP_ROUTING        = 0x0040,  // Can route to other units
    CAP_PERSISTENCE    = 0x0080,  // Can persist data
    CAP_PARALLEL       = 0x0100,  // Can run in parallel
    CAP_STATEFUL       = 0x0200,  // Maintains state
    CAP_NUMA_AWARE     = 0x0400,  // NUMA optimized
    CAP_SIMD_OPTIMIZED = 0x0800,  // Uses SIMD instructions
    CAP_LOCK_FREE      = 0x1000,  // Lock-free implementation
    CAP_ZERO_COPY      = 0x2000,  // Zero-copy data passing
    CAP_REAL_TIME      = 0x4000,  // Real-time capable
    CAP_ML_ENHANCED    = 0x8000   // Machine learning enhanced
};

// ==========================================================================
// PROCESSING UNIT TYPE - Strongly typed enumeration
// ==========================================================================

// Origin: Enumeration for unit types
// Scope: Global type identification
enum class ProcessingUnitType : u32 {
    INVALID    = 0,
    TICK       = 1,
    ORDER      = 2,
    DATA       = 3,
    BATCH      = 4,
    INTERPOLATION = 5,
    AGGREGATION = 6,
    ROUTING    = 7,
    PERSISTENCE = 8,
    ML_INFERENCE = 9,
    CUSTOM     = 100  // User-defined types start here
};

// ==========================================================================
// PROCESSING UNIT STATE - Atomic state machine
// ==========================================================================

// Origin: Enumeration for unit states
// Scope: Instance state tracking
enum class ProcessingUnitState : u8 {
    UNINITIALIZED = 0,
    INITIALIZING  = 1,
    READY         = 2,
    PROCESSING    = 3,
    PAUSED        = 4,
    ERROR         = 5,
    SHUTTING_DOWN = 6,
    TERMINATED    = 7
};

// ==========================================================================
// IPROCESSINGUNIT - PURE VIRTUAL INTERFACE
// ==========================================================================

// Origin: Pure interface for all processing units
// This interface defines the CONTRACT that ALL processing units MUST fulfill
class IProcessingUnit {
public:
    // Virtual destructor for proper cleanup
    virtual ~IProcessingUnit() = default;
    
    // ======================================================================
    // LIFECYCLE METHODS - MUST BE IMPLEMENTED
    // ======================================================================
    
    // Origin: Initialize the processing unit
    // Input: config (ProcessingUnitConfig) - Configuration parameters
    // Output: ResultCode - SUCCESS or specific error
    // Scope: Called once during setup
    virtual ResultCode initialize(const ProcessingUnitConfig& config) noexcept = 0;
    
    // Origin: Validate unit configuration and state
    // Input: None
    // Output: true if valid, false otherwise
    // Scope: Can be called anytime to verify integrity
    virtual bool validate() const noexcept = 0;
    
    // Origin: Shutdown the processing unit
    // Input: None
    // Output: ResultCode - SUCCESS or specific error
    // Scope: Called once during teardown
    virtual ResultCode shutdown() noexcept = 0;
    
    // ======================================================================
    // PROCESSING METHODS - CORE FUNCTIONALITY
    // ======================================================================
    
    // Origin: Process a single tick
    // Input: sessionId - Session identifier
    //        tick - Market tick data
    // Output: ProcessResult - Result of processing
    // Scope: Called for each tick in real-time
    virtual ProcessResult processTick(SessionId sessionId, const Tick& tick) noexcept = 0;
    
    // Origin: Process a batch of ticks
    // Input: sessionId - Session identifier
    //        ticks - Array of ticks
    //        count - Number of ticks
    // Output: ProcessResult - Result of batch processing
    // Scope: Called for batch processing
    virtual ProcessResult processBatch(SessionId sessionId, 
                                      const Tick* ticks, 
                                      usize count) noexcept = 0;
    
    // Origin: Process stream data
    // Input: sessionId - Session identifier
    //        streamData - Stream packet
    // Output: ProcessResult - Result of stream processing
    // Scope: Called for streaming data
    virtual ProcessResult processStream(SessionId sessionId,
                                       const StreamData& streamData) noexcept = 0;
    
    // ======================================================================
    // ROUTING METHODS - INTER-UNIT COMMUNICATION
    // ======================================================================
    
    // Origin: Connect to another processing unit
    // Input: targetUnit - Target unit ID
    //        connectionType - Type of connection
    // Output: ResultCode - SUCCESS or specific error
    // Scope: Setup phase for routing
    virtual ResultCode connectTo(ProcessingUnitId targetUnit,
                                 u32 connectionType) noexcept = 0;
    
    // Origin: Disconnect from another processing unit
    // Input: targetUnit - Target unit ID
    // Output: ResultCode - SUCCESS or specific error
    // Scope: Cleanup phase for routing
    virtual ResultCode disconnectFrom(ProcessingUnitId targetUnit) noexcept = 0;
    
    // Origin: Route data to connected units
    // Input: data - Data to route
    //        size - Size of data
    // Output: u32 - Number of units data was routed to
    // Scope: Runtime data routing
    virtual u32 routeToConnected(const void* data, usize size) noexcept = 0;
    
    // ======================================================================
    // METADATA METHODS - UNIT INFORMATION
    // ======================================================================
    
    // Origin: Get the unit type
    // Input: None
    // Output: ProcessingUnitType - Type of this unit
    // Scope: Constant for unit lifetime
    virtual ProcessingUnitType getType() const noexcept = 0;
    
    // Origin: Get unit capabilities
    // Input: None
    // Output: u64 - Bitfield of capabilities
    // Scope: Constant for unit lifetime
    virtual u64 getCapabilities() const noexcept = 0;
    
    // Origin: Get current state
    // Input: None
    // Output: ProcessingUnitState - Current state
    // Scope: Changes during lifecycle
    virtual ProcessingUnitState getState() const noexcept = 0;
    
    // Origin: Get unit ID
    // Input: None
    // Output: ProcessingUnitId - Unique identifier
    // Scope: Constant for unit lifetime
    virtual ProcessingUnitId getId() const noexcept = 0;
    
    // Origin: Get NUMA node affinity
    // Input: None
    // Output: i32 - NUMA node (-1 if not NUMA-aware)
    // Scope: Constant after initialization
    virtual i32 getNumaNode() const noexcept = 0;
    
    // ======================================================================
    // METRICS METHODS - PERFORMANCE MONITORING
    // ======================================================================
    
    // Origin: Get performance metrics
    // Input: None
    // Output: ProcessingUnitMetrics - Current metrics
    // Scope: Runtime performance data
    virtual ProcessingUnitMetrics getMetrics() const noexcept = 0;
    
    // Origin: Reset metrics
    // Input: None
    // Output: None
    // Scope: Administrative operation
    virtual void resetMetrics() noexcept = 0;
    
    // ======================================================================
    // CONFIGURATION METHODS - RUNTIME CONFIGURATION
    // ======================================================================
    
    // Origin: Update configuration at runtime
    // Input: config - New configuration
    // Output: ResultCode - SUCCESS or specific error
    // Scope: Runtime reconfiguration
    virtual ResultCode reconfigure(const ProcessingUnitConfig& config) noexcept = 0;
    
    // Origin: Get current configuration
    // Input: None
    // Output: ProcessingUnitConfig - Current configuration
    // Scope: Configuration query
    virtual ProcessingUnitConfig getConfiguration() const noexcept = 0;
};

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify interface has no data members (pure interface)
static_assert(sizeof(IProcessingUnit) == sizeof(void*), 
              "IProcessingUnit must be pure interface with no data members");

// Verify no locks or mutexes
ENFORCE_NO_MUTEX(IProcessingUnit);

// Mark interface complete
ENFORCE_HEADER_COMPLETE(Core_IProcessingUnit);

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_IPROCESSINGUNIT_H