//===--- Core_DAGNode.cpp - DAG Node Implementation ---------------------===//
//
// COMPILATION LEVEL: 5 (After Core_DAGTypes)
// ORIGIN: Implementation of Core_DAGNode.h
//
// PSYCHOTIC PRECISION: ZERO ALLOCATIONS, PURE PERFORMANCE
//===----------------------------------------------------------------------===//

#include "Core_DAGNode.h"

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// GLOBAL NODE POOL - Singleton instance for entire system
// ============================================================================

// PSYCHOTIC: Single global pool for 100K nodes initially (25MB)
// Can be increased but 10M would be 2.5GB - too large for static allocation
static DAGNodePool<100000> g_globalNodePool;

// Get global node pool
DAGNodePool<100000>& getGlobalNodePool() noexcept {
    return g_globalNodePool;
}

// ============================================================================
// NODE VALIDATION
// ============================================================================

// Validate node configuration
bool validateNode(const DAGNode& node) noexcept {
    // Check ID validity
    if (!isValidNodeId(node.nodeId)) return false;
    if (!isValidDAGId(node.dagId)) return false;
    
    // Check degree constraints
    if (node.inDegree > 6 || node.outDegree > 6) return false;
    
    // Check buffer validity
    for (u32 i = 0; i < node.inDegree; ++i) {
        if (node.inputBuffers[i] == INVALID_BUFFER_ID) return false;
    }
    
    for (u32 i = 0; i < node.outDegree; ++i) {
        if (node.outputBuffers[i] == INVALID_BUFFER_ID) return false;
    }
    
    // Check NUMA node validity
    if (node.numaNode < -1 || node.numaNode > 8) return false;
    
    // Check SIMD width validity
    if (node.simdWidth != 128 && node.simdWidth != 256 && node.simdWidth != 512) {
        return false;
    }
    
    return true;
}

// ============================================================================
// NODE FACTORY FUNCTIONS
// ============================================================================

// Create input node for stream ingestion
DAGNode* createInputNode(DAGId dagId, StreamId streamId) noexcept {
    DAGNode* node = g_globalNodePool.allocate();
    if (!node) return nullptr;
    
    node->nodeId = generateNodeId();
    node->dagId = dagId;
    node->nodeType = ProcessingUnitType::MARKET_DATA_RECEIVER;
    node->priority = ExecutionPriority::HIGH;
    node->unitId = static_cast<ProcessingUnitId>(streamId);
    node->state.store(static_cast<u32>(NodeState::READY));
    
    // Input nodes have no predecessors
    node->inDegree = 0;
    
    // Allocate output buffer
    node->outputBuffers[0] = static_cast<DataBufferId>(node->nodeId.value);
    
    return node;
}

// Create processing node
DAGNode* createProcessingNode(DAGId dagId, ProcessingUnitType type) noexcept {
    DAGNode* node = g_globalNodePool.allocate();
    if (!node) return nullptr;
    
    node->nodeId = generateNodeId();
    node->dagId = dagId;
    node->nodeType = type;
    node->priority = ExecutionPriority::NORMAL;
    node->unitId = static_cast<ProcessingUnitId>(node->nodeId.value);
    node->state.store(static_cast<u32>(NodeState::WAITING));
    
    // Allocate buffers
    node->inputBuffers[0] = static_cast<DataBufferId>(node->nodeId.value * 2);
    node->outputBuffers[0] = static_cast<DataBufferId>(node->nodeId.value * 2 + 1);
    
    return node;
}

// Create output node for result publishing
DAGNode* createOutputNode(DAGId dagId, StreamId streamId) noexcept {
    DAGNode* node = g_globalNodePool.allocate();
    if (!node) return nullptr;
    
    node->nodeId = generateNodeId();
    node->dagId = dagId;
    node->nodeType = ProcessingUnitType::RESULT_PUBLISHER;
    node->priority = ExecutionPriority::HIGH;
    node->unitId = static_cast<ProcessingUnitId>(streamId);
    node->state.store(static_cast<u32>(NodeState::WAITING));
    
    // Output nodes have no successors
    node->outDegree = 0;
    
    // Allocate input buffer
    node->inputBuffers[0] = static_cast<DataBufferId>(node->nodeId.value);
    
    return node;
}

// ============================================================================
// NODE CONNECTION FUNCTIONS
// ============================================================================

// Connect two nodes (source -> target)
bool connectNodes(DAGNode* source, DAGNode* target) noexcept {
    if (!source || !target) return false;
    
    // Add successor to source
    if (!source->addSuccessor(target->nodeId)) {
        return false;  // Source has too many successors
    }
    
    // Add predecessor to target
    if (!target->addPredecessor(source->nodeId)) {
        // Rollback source modification
        source->removeSuccessor(target->nodeId);
        return false;  // Target has too many predecessors
    }
    
    // Connect buffers: source output -> target input
    u32 sourceOutIdx = source->outDegree - 1;
    u32 targetInIdx = target->inDegree - 1;
    
    if (sourceOutIdx < 4 && targetInIdx < 4) {
        target->inputBuffers[targetInIdx] = source->outputBuffers[sourceOutIdx];
    }
    
    return true;
}

// Disconnect two nodes
bool disconnectNodes(DAGNode* source, DAGNode* target) noexcept {
    if (!source || !target) return false;
    
    bool removed1 = source->removeSuccessor(target->nodeId);
    bool removed2 = target->removePredecessor(source->nodeId);
    
    return removed1 && removed2;
}

// ============================================================================
// NODE EXECUTION HELPERS
// ============================================================================

// Prepare node for execution
bool prepareNodeExecution(DAGNode* node) noexcept {
    if (!node) return false;
    
    // Try to transition from READY to EXECUTING
    u32 expected = static_cast<u32>(NodeState::READY);
    u32 desired = static_cast<u32>(NodeState::EXECUTING);
    
    if (!node->state.compare_exchange_strong(expected, desired,
                                            std::memory_order_acquire,
                                            std::memory_order_release)) {
        return false;  // Node not in READY state
    }
    
    // Update last execution time
    node->lastExecutionTime.store(createTimestamp(), std::memory_order_release);
    
    return true;
}

// Complete node execution
void completeNodeExecution(DAGNode* node, u64 latencyNs, u64 bytesProcessed) noexcept {
    if (!node) return;
    
    // Update statistics
    node->updateStats(latencyNs, bytesProcessed);
    
    // Transition from EXECUTING to COMPLETED
    u32 expected = static_cast<u32>(NodeState::EXECUTING);
    u32 desired = static_cast<u32>(NodeState::COMPLETED);
    
    node->state.compare_exchange_strong(expected, desired,
                                       std::memory_order_release,
                                       std::memory_order_relaxed);
}

// Mark node as having error
void markNodeError(DAGNode* node, u32 errorCode) noexcept {
    if (!node) return;
    
    node->errorCount.fetch_add(1, std::memory_order_relaxed);
    node->state.store(static_cast<u32>(NodeState::ERROR), std::memory_order_release);
    
    // Store error code in flags
    node->flags = errorCode;
}

// Reset node for reuse
void resetNode(DAGNode* node) noexcept {
    if (!node) return;
    
    // Reset execution state
    node->state.store(static_cast<u32>(NodeState::UNINITIALIZED), std::memory_order_release);
    node->resetStats();
    
    // Clear predecessors and successors
    node->inDegree.store(0, std::memory_order_release);
    node->outDegree.store(0, std::memory_order_release);
    
    for (u32 i = 0; i < 6; ++i) {
        node->predecessors[i] = INVALID_NODE_ID;
        node->successors[i] = INVALID_NODE_ID;
    }
    
    // Clear buffers
    for (u32 i = 0; i < 4; ++i) {
        node->inputBuffers[i] = INVALID_BUFFER_ID;
        node->outputBuffers[i] = INVALID_BUFFER_ID;
    }
    
    // Reset other fields
    node->pendingMessages.store(0, std::memory_order_release);
    node->dataVersion.store(0, std::memory_order_release);
}

// ============================================================================
// NODE OPTIMIZATION FUNCTIONS
// ============================================================================

// Set NUMA affinity for node
void setNodeNumaAffinity(DAGNode* node, i32 numaNode) noexcept {
    if (!node) return;
    node->numaNode = numaNode;
}

// Set CPU affinity for node
void setNodeCpuAffinity(DAGNode* node, u32 cpuMask) noexcept {
    if (!node) return;
    node->cpuAffinity = cpuMask;
}

// Enable SIMD optimization
void enableNodeSIMD(DAGNode* node, u32 width) noexcept {
    if (!node) return;
    if (width == 128 || width == 256 || width == 512) {
        node->simdWidth = width;
    }
}

// Set cache hints
void setNodeCacheHints(DAGNode* node, u32 hints) noexcept {
    if (!node) return;
    node->cacheHints = hints;
}

AARENDOCORE_NAMESPACE_END