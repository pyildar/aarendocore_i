//===--- Core_DAGBuilder.h - DAG Construction and Validation ------------===//
//
// COMPILATION LEVEL: 7 (After DAGNode, MessageTypes, StreamMultiplexer)
// DEPENDENCIES: 
//   - Core_Types.h (u32, u64, AtomicU32)
//   - Core_DAGTypes.h (DAGId, NodeId, ProcessingUnitType)
//   - Core_DAGNode.h (DAGNode, DAGNodePool)
//   - Core_MessageTypes.h (Message)
// ORIGIN: NEW - Builds and validates DAG topology
//
// PSYCHOTIC PRECISION: ZERO-ALLOCATION DAG CONSTRUCTION
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_DAGBUILDER_H
#define AARENDOCORE_CORE_DAGBUILDER_H

#include "Core_Platform.h"
#include "Core_Types.h"
#include "Core_DAGTypes.h"
#include "Core_DAGNode.h"
#include "Core_MessageTypes.h"
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_vector.h>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// DAG TOPOLOGY DESCRIPTOR
// ============================================================================
// Origin: Describes the structure of a DAG
// Scope: Used to build DAG instances
struct DAGTopology {
    static constexpr u32 MAX_NODES = 1024;      // Max nodes per DAG
    static constexpr u32 MAX_EDGES = 4096;      // Max edges per DAG
    
    // Node descriptors
    struct NodeDescriptor {
        NodeId nodeId;
        ProcessingUnitType type;
        ExecutionPriority priority;
        u32 inputCount;
        u32 outputCount;
        i32 numaNode;           // NUMA affinity (-1 = any)
        u32 cpuAffinity;        // CPU mask (0 = any)
        
        NodeDescriptor() noexcept 
            : nodeId(INVALID_NODE_ID)
            , type(ProcessingUnitType::MARKET_DATA_RECEIVER)
            , priority(ExecutionPriority::NORMAL)
            , inputCount(0)
            , outputCount(0)
            , numaNode(-1)
            , cpuAffinity(0) {}
    };
    
    // Edge descriptors
    struct EdgeDescriptor {
        NodeId sourceNode;
        NodeId targetNode;
        u32 bufferSize;         // Buffer size in messages
        TransformationType transformType;
        
        EdgeDescriptor() noexcept 
            : sourceNode(INVALID_NODE_ID)
            , targetNode(INVALID_NODE_ID)
            , bufferSize(1024)
            , transformType(TransformationType::PASSTHROUGH) {}
    };
    
    // Topology data
    tbb::concurrent_vector<NodeDescriptor> nodes;
    tbb::concurrent_vector<EdgeDescriptor> edges;
    DAGType dagType;
    u32 version;
    
    // Constructor
    DAGTopology() noexcept 
        : nodes()
        , edges()
        , dagType(DAGType::STATIC_TOPOLOGY | DAGType::LOCK_FREE)
        , version(1) {}
    
    // Add node to topology
    void addNode(const NodeDescriptor& node) noexcept {
        nodes.push_back(node);
    }
    
    // Add edge to topology
    void addEdge(const EdgeDescriptor& edge) noexcept {
        edges.push_back(edge);
    }
    
    // Clear topology
    void clear() noexcept {
        nodes.clear();
        edges.clear();
    }
};

// ============================================================================
// DAG VALIDATION RESULT
// ============================================================================
// Origin: Result of DAG validation
// Scope: Returned by validation methods
struct ValidationResult {
    bool isValid;
    u32 errorCode;
    NodeId problematicNode;
    char errorMessage[256];
    
    // Error codes
    enum ErrorCode : u32 {
        NO_ERROR = 0,
        CYCLE_DETECTED = 1,
        INVALID_NODE = 2,
        INVALID_EDGE = 3,
        TOO_MANY_INPUTS = 4,
        TOO_MANY_OUTPUTS = 5,
        MEMORY_ALLOCATION_FAILED = 6,
        DUPLICATE_NODE_ID = 7,
        ORPHANED_NODE = 8,
        INCOMPATIBLE_TYPES = 9
    };
    
    ValidationResult() noexcept 
        : isValid(true)
        , errorCode(NO_ERROR)
        , problematicNode(INVALID_NODE_ID)
        , errorMessage{} {}
};

// ============================================================================
// DAG INSTANCE - Runtime DAG
// ============================================================================
// Origin: A built and validated DAG ready for execution
// Scope: Created by DAGBuilder
class DAGInstance {
private:
    DAGId dagId;
    tbb::concurrent_vector<DAGNode*> nodes;
    tbb::concurrent_hash_map<NodeId, DAGNode*, NodeIdHashCompare> nodeMap;
    tbb::concurrent_vector<NodeId> topologicalOrder;
    AtomicU32 state;
    AtomicU64 version;
    
public:
    // Constructor
    DAGInstance(DAGId id) noexcept 
        : dagId(id)
        , nodes()
        , nodeMap()
        , topologicalOrder()
        , state(static_cast<u32>(DAGState::UNINITIALIZED))
        , version(0) {}
    
    // Destructor
    ~DAGInstance() noexcept;
    
    // Add node to instance
    bool addNode(DAGNode* node) noexcept;
    
    // Get node by ID
    DAGNode* getNode(NodeId id) noexcept;
    
    // Set topological order
    void setTopologicalOrder(const tbb::concurrent_vector<NodeId>& order) noexcept {
        topologicalOrder = order;
    }
    
    // Get topological order
    const tbb::concurrent_vector<NodeId>& getTopologicalOrder() const noexcept {
        return topologicalOrder;
    }
    
    // Get all nodes
    const tbb::concurrent_vector<DAGNode*>& getNodes() const noexcept {
        return nodes;
    }
    
    // Get DAG ID
    DAGId getId() const noexcept { return dagId; }
    
    // Get state
    DAGState getState() const noexcept {
        return static_cast<DAGState>(state.load(std::memory_order_acquire));
    }
    
    // Set state
    void setState(DAGState newState) noexcept {
        state.store(static_cast<u32>(newState), std::memory_order_release);
    }
};

// ============================================================================
// DAG BUILDER - Main builder class
// ============================================================================
// Origin: Constructs and validates DAGs
// Scope: Central DAG construction facility
class DAGBuilder {
private:
    // Node pool reference (from global pool)
    DAGNodePool<100000>* nodePool;
    
    // Statistics
    struct BuildStats {
        AtomicU64 dagsBuilt;
        AtomicU64 nodesAllocated;
        AtomicU64 edgesCreated;
        AtomicU64 validationsFailed;
    } stats;
    
    // Cycle detection colors for DFS
    enum class NodeColor : u32 {
        WHITE = 0,  // Not visited
        GRAY = 1,   // Being processed
        BLACK = 2   // Completed
    };
    
public:
    // Constructor
    DAGBuilder() noexcept;
    
    // Destructor
    ~DAGBuilder() noexcept = default;
    
    // Build DAG from topology
    DAGInstance* buildDAG(const DAGTopology& topology) noexcept;
    
    // Validate topology
    ValidationResult validateTopology(const DAGTopology& topology) noexcept;
    
    // Validate DAG instance
    ValidationResult validateDAG(const DAGInstance* dag) noexcept;
    
    // Optimize DAG for execution
    void optimizeDAG(DAGInstance* dag) noexcept;
    
    // Statistics
    u64 getDAGsBuilt() const noexcept { return stats.dagsBuilt.load(); }
    u64 getNodesAllocated() const noexcept { return stats.nodesAllocated.load(); }
    u64 getEdgesCreated() const noexcept { return stats.edgesCreated.load(); }
    u64 getValidationsFailed() const noexcept { return stats.validationsFailed.load(); }
    
private:
    // Helper methods
    bool detectCycles(const DAGTopology& topology) noexcept;
    bool detectCyclesInDAG(const DAGInstance* dag) noexcept;
    bool topologicalSort(DAGInstance* dag) noexcept;
    bool allocateBuffers(DAGInstance* dag) noexcept;
    void setNumaAffinity(DAGInstance* dag) noexcept;
    bool dfsVisit(NodeId nodeId, 
                  tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>& colors,
                  tbb::concurrent_vector<NodeId>& order,
                  const tbb::concurrent_hash_map<NodeId, tbb::concurrent_vector<NodeId>, NodeIdHashCompare>& adjList) noexcept;
};

// ============================================================================
// DAG BUILDER FACTORY FUNCTIONS
// ============================================================================

// Create a simple linear DAG (A -> B -> C)
DAGTopology createLinearDAG(u32 nodeCount, ProcessingUnitType type) noexcept;

// Create a fan-out DAG (A -> [B, C, D])
DAGTopology createFanOutDAG(u32 fanOutFactor, ProcessingUnitType rootType) noexcept;

// Create a fan-in DAG ([A, B, C] -> D)
DAGTopology createFanInDAG(u32 fanInFactor, ProcessingUnitType sinkType) noexcept;

// Create a diamond DAG (A -> [B, C] -> D)
DAGTopology createDiamondDAG(ProcessingUnitType type) noexcept;

// Create a complex multi-stage DAG
DAGTopology createMultiStageDAG(u32 stages, u32 nodesPerStage) noexcept;

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_DAGBUILDER_H