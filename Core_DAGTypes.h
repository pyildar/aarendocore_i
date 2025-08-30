//===--- Core_DAGTypes.h - DAG Type Definitions -------------------------===//
//
// COMPILATION LEVEL: 4 (After Core_Types, Core_MessageTypes)
// DEPENDENCIES: 
//   - Core_Types.h (u32, u64, AtomicU32)
//   - Core_MessageTypes.h (MessageType)
// ORIGIN: NEW - Defines all DAG-related types and enums
//
// PSYCHOTIC PRECISION: EVERY TYPE TRACED, ZERO AMBIGUITY
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_DAGTYPES_H
#define AARENDOCORE_CORE_DAGTYPES_H

#include "Core_Platform.h"      // LEVEL 0: Platform macros
#include "Core_Types.h"         // LEVEL 1: Basic types
#include "Core_MessageTypes.h"  // LEVEL 3: Message types
#include <functional>           // For std::hash

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// DAG ID TYPES - Strongly typed IDs to prevent mixing
// ============================================================================

// Origin: Unique DAG identifier
// Scope: Identifies a complete DAG
struct DAGId {
    u64 value;
    
    constexpr DAGId() noexcept : value(0) {}
    constexpr explicit DAGId(u64 v) noexcept : value(v) {}
    
    constexpr bool operator==(const DAGId& other) const noexcept {
        return value == other.value;
    }
    
    constexpr bool operator!=(const DAGId& other) const noexcept {
        return value != other.value;
    }
    
    constexpr bool operator<(const DAGId& other) const noexcept {
        return value < other.value;
    }
};

// Origin: Unique node identifier within DAG
// Scope: Identifies a single node
struct NodeId {
    u64 value;
    
    constexpr NodeId() noexcept : value(0) {}
    constexpr explicit NodeId(u64 v) noexcept : value(v) {}
    
    constexpr bool operator==(const NodeId& other) const noexcept {
        return value == other.value;
    }
    
    constexpr bool operator!=(const NodeId& other) const noexcept {
        return value != other.value;
    }
    
    constexpr bool operator<(const NodeId& other) const noexcept {
        return value < other.value;
    }
};

// Origin: Processing unit identifier
// Scope: Links node to actual processing unit
using ProcessingUnitId = u64;

// Origin: Data buffer identifier
// Scope: Identifies data buffers for nodes
using DataBufferId = u32;

// Origin: Stream identifier
// Scope: Identifies input/output streams
using StreamId = u32;

// Invalid ID constants
static constexpr NodeId INVALID_NODE_ID{0};
static constexpr DAGId INVALID_DAG_ID{0};
static constexpr ProcessingUnitId INVALID_UNIT_ID{0};
static constexpr DataBufferId INVALID_BUFFER_ID{0};
static constexpr StreamId INVALID_STREAM_ID{0};

// ============================================================================
// DAG PROPERTIES - Bitfield for DAG characteristics
// ============================================================================
enum class DAGType : u32 {
    STATIC_TOPOLOGY     = 0x0001,  // Fixed at creation
    DYNAMIC_TOPOLOGY    = 0x0002,  // Runtime modification
    HIERARCHICAL        = 0x0004,  // Parent-child DAGs
    DISTRIBUTED         = 0x0008,  // Cross-node execution
    CYCLIC_DETECTION    = 0x0010,  // Prevents cycles
    DEPENDENCY_ORDERED  = 0x0020,  // Topological sort
    PRIORITY_SCHEDULED  = 0x0040,  // Priority execution
    NUMA_AWARE         = 0x0080,  // NUMA optimization
    SIMD_VECTORIZED    = 0x0100,  // SIMD operations
    LOCK_FREE          = 0x0200   // Lock-free updates
};

// Combine DAG types
inline DAGType operator|(DAGType a, DAGType b) noexcept {
    return static_cast<DAGType>(static_cast<u32>(a) | static_cast<u32>(b));
}

// Check DAG type
inline bool hasDAGType(DAGType flags, DAGType type) noexcept {
    return (static_cast<u32>(flags) & static_cast<u32>(type)) != 0;
}

// ============================================================================
// NODE STATE - Execution state of a node
// ============================================================================
enum class NodeState : u32 {
    UNINITIALIZED  = 0x00,  // Not yet initialized
    READY          = 0x01,  // Ready to execute
    WAITING        = 0x02,  // Waiting for dependencies
    EXECUTING      = 0x03,  // Currently executing
    COMPLETED      = 0x04,  // Execution completed
    ERROR          = 0x05,  // Error state
    SUSPENDED      = 0x06,  // Temporarily suspended
    TERMINATED     = 0x07   // Permanently stopped
};

// ============================================================================
// DAG STATE - Runtime state of entire DAG
// ============================================================================
enum class DAGState : u32 {
    UNINITIALIZED  = 0x00,  // Not yet initialized
    READY          = 0x01,  // Ready to execute
    RUNNING        = 0x02,  // Currently running
    PAUSED         = 0x03,  // Temporarily paused
    COMPLETED      = 0x04,  // Execution completed
    ERROR          = 0x05,  // Error state
    TERMINATED     = 0x06   // Permanently stopped
};

// ============================================================================
// PROCESSING UNIT TYPES - What executes in nodes
// ============================================================================
enum class ProcessingUnitType : u32 {
    // DATA INGESTION UNITS (0x1000 range)
    MARKET_DATA_RECEIVER    = 0x1001,  // Receives market ticks/bars
    STREAM_NORMALIZER      = 0x1002,  // Normalizes different data formats
    TIMESTAMP_ALIGNER      = 0x1003,  // Aligns timestamps across streams
    
    // TRANSFORMATION UNITS (0x2000 range)
    INTERPOLATOR           = 0x2001,  // Time-series interpolation
    AGGREGATOR            = 0x2002,  // Data aggregation (OHLCV)
    FILTER               = 0x2003,  // Signal filtering
    CALCULATOR           = 0x2004,  // Math operations
    
    // ANALYSIS UNITS (0x3000 range)
    PATTERN_DETECTOR      = 0x3001,  // Pattern recognition
    INDICATOR_COMPUTER    = 0x3002,  // Technical indicators
    ML_PREDICTOR         = 0x3003,  // ML predictions
    STATISTICAL_ANALYZER  = 0x3004,  // Statistical analysis
    
    // DECISION UNITS (0x4000 range)
    SIGNAL_GENERATOR      = 0x4001,  // Trading signals
    RISK_EVALUATOR       = 0x4002,  // Risk assessment
    POSITION_SIZER       = 0x4003,  // Position sizing
    
    // OUTPUT UNITS (0x5000 range)
    ORDER_ROUTER         = 0x5001,  // Route orders
    PERSISTENCE_WRITER   = 0x5002,  // Write to storage
    ALERT_DISPATCHER     = 0x5003,  // Send alerts
    RESULT_PUBLISHER     = 0x5004   // Publish results
};

// ============================================================================
// DEPENDENCY TYPE - Types of dependencies between nodes
// ============================================================================
enum class DependencyType : u32 {
    DATA_DEPENDENCY    = 0x01,  // Needs output from predecessor
    TEMPORAL_DEPENDENCY = 0x02,  // Time-based ordering
    RESOURCE_DEPENDENCY = 0x04,  // Shared resource access
    BARRIER_DEPENDENCY  = 0x08,  // Synchronization barrier
    CONDITIONAL_DEPENDENCY = 0x10  // Conditional execution
};

// Combine dependency types
inline DependencyType operator|(DependencyType a, DependencyType b) noexcept {
    return static_cast<DependencyType>(static_cast<u32>(a) | static_cast<u32>(b));
}

// ============================================================================
// EXECUTION PRIORITY - Node execution priority
// ============================================================================
enum class ExecutionPriority : u32 {
    CRITICAL    = 0,    // Highest priority - execute immediately
    HIGH        = 1,    // High priority
    NORMAL      = 2,    // Normal priority
    LOW         = 3,    // Low priority
    BACKGROUND  = 4     // Lowest priority - background tasks
};

// ============================================================================
// TRANSFORMATION TYPE - Stream transformation operations
// ============================================================================
enum class TransformationType : u32 {
    PASSTHROUGH      = 0x00,  // Direct pass
    AGGREGATE        = 0x01,  // Combine multiple inputs
    SPLIT            = 0x02,  // Split to multiple outputs
    INTERPOLATE      = 0x03,  // Time-align via interpolation
    FILTER           = 0x04,  // Apply filtering
    TRANSFORM        = 0x05,  // Custom transformation
    SYNCHRONIZE      = 0x06   // Time synchronization
};

// ============================================================================
// DATA TYPE - Types of data flowing through DAG
// ============================================================================
enum class DataType : u32 {
    TICK           = 0x01,  // Tick data
    BAR            = 0x02,  // OHLCV bar
    INDICATOR      = 0x03,  // Indicator value
    SIGNAL         = 0x04,  // Trading signal
    ORDER          = 0x05,  // Order data
    POSITION       = 0x06,  // Position data
    RISK_METRIC    = 0x07,  // Risk measurement
    ML_OUTPUT      = 0x08,  // ML model output
    CUSTOM         = 0xFF   // Custom data type
};

// ============================================================================
// DAG METADATA - Information about a DAG
// ============================================================================
struct alignas(AARENDOCORE_CACHE_LINE_SIZE) DAGMetadata {
    DAGId id;                    // Unique DAG ID
    char name[32];               // DAG name
    DAGType type;                // DAG properties
    u32 nodeCount;               // Number of nodes
    u32 edgeCount;               // Number of edges
    u64 creationTime;            // Creation timestamp
    u64 lastExecutionTime;       // Last execution
    AtomicU64 executionCount;    // Total executions
    AtomicU32 state;             // DAG state (NodeState enum)
    i32 numaNode;                // NUMA node affinity
    u32 priority;                // Execution priority
    char padding[4];             // Alignment padding
};

// static_assert(sizeof(DAGMetadata) == AARENDOCORE_CACHE_LINE_SIZE,
//               "DAGMetadata must be exactly one cache line");

// ============================================================================
// NODE DEPENDENCY - Dependency information
// ============================================================================
struct Dependency {
    NodeId source;              // Source node
    NodeId target;              // Target node
    DependencyType type;        // Type of dependency
    u32 weight;                 // Dependency weight/priority
    u64 minDelayNs;            // Minimum delay between executions
    u64 maxDelayNs;            // Maximum delay (timeout)
};

// ============================================================================
// EDGE INFORMATION - Edge in the DAG
// ============================================================================
struct Edge {
    NodeId from;                // Source node
    NodeId to;                  // Target node
    DataType dataType;          // Type of data flowing
    u32 capacity;               // Edge capacity (messages/sec)
    AtomicU64 messagesTransferred;  // Statistics
};

// ============================================================================
// EXECUTION STATISTICS - Performance metrics
// ============================================================================
struct ExecutionStats {
    AtomicU64 totalExecutions;   // Total node executions
    AtomicU64 totalLatencyNs;    // Total execution time
    AtomicU64 minLatencyNs;      // Minimum latency
    AtomicU64 maxLatencyNs;      // Maximum latency
    AtomicU64 lastExecutionTime; // Last execution timestamp
    AtomicU32 errorCount;        // Number of errors
    AtomicU32 timeoutCount;      // Number of timeouts
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Generate unique node ID
inline NodeId generateNodeId() noexcept {
    static AtomicU64 nextId{1};
    return NodeId{nextId.fetch_add(1, std::memory_order_relaxed)};
}

// Generate unique DAG ID
inline DAGId generateDAGId() noexcept {
    static AtomicU64 nextId{1};
    return DAGId{nextId.fetch_add(1, std::memory_order_relaxed)};
}

// Check if node ID is valid
inline bool isValidNodeId(NodeId id) noexcept {
    return id != INVALID_NODE_ID;
}

// Check if DAG ID is valid
inline bool isValidDAGId(DAGId id) noexcept {
    return id != INVALID_DAG_ID;
}

// Convert node state to string (for debugging)
inline const char* nodeStateToString(NodeState state) noexcept {
    switch (state) {
        case NodeState::UNINITIALIZED: return "UNINITIALIZED";
        case NodeState::READY: return "READY";
        case NodeState::WAITING: return "WAITING";
        case NodeState::EXECUTING: return "EXECUTING";
        case NodeState::COMPLETED: return "COMPLETED";
        case NodeState::ERROR: return "ERROR";
        case NodeState::SUSPENDED: return "SUSPENDED";
        case NodeState::TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// HASH COMPARE FOR TBB - Custom hash_compare for NodeId and DAGId
// ============================================================================
// PSYCHOTIC: TBB needs a special hash_compare class with hash() and equal() methods!

template<typename IdType>
struct IdHashCompare {
    std::size_t hash(const IdType& id) const noexcept {
        return std::hash<u64>()(id.value);
    }
    
    bool equal(const IdType& a, const IdType& b) const noexcept {
        return a.value == b.value;
    }
};

// Specific typedefs for convenience
using NodeIdHashCompare = IdHashCompare<NodeId>;
using DAGIdHashCompare = IdHashCompare<DAGId>;

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_DAGTYPES_H