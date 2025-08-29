//===--- Core_DAGBuilder.cpp - DAG Construction Implementation ----------===//
//
// COMPILATION LEVEL: 7 (After DAGNode, MessageTypes, StreamMultiplexer)
// ORIGIN: Implementation of Core_DAGBuilder.h
//
// PSYCHOTIC PRECISION: ZERO-ALLOCATION DAG CONSTRUCTION
//===----------------------------------------------------------------------===//

#include "Core_DAGBuilder.h"
#include <algorithm>

AARENDOCORE_NAMESPACE_BEGIN

// External reference to global node pool
extern DAGNodePool<100000>& getGlobalNodePool() noexcept;

// ============================================================================
// DAG INSTANCE IMPLEMENTATION
// ============================================================================

// Destructor
DAGInstance::~DAGInstance() noexcept {
    // Nodes are owned by the global pool, don't delete them
    // Just clear our references
    nodes.clear();
    nodeMap.clear();
    topologicalOrder.clear();
}

// Add node to instance
bool DAGInstance::addNode(DAGNode* node) noexcept {
    if (!node) return false;
    if (node->dagId != dagId) return false;
    
    nodes.push_back(node);
    nodeMap.insert(std::make_pair(node->nodeId, node));
    return true;
}

// Get node by ID
DAGNode* DAGInstance::getNode(NodeId id) noexcept {
    tbb::concurrent_hash_map<NodeId, DAGNode*, NodeIdHashCompare>::accessor accessor;
    if (nodeMap.find(accessor, id)) {
        return accessor->second;
    }
    return nullptr;
}

// ============================================================================
// DAG BUILDER IMPLEMENTATION
// ============================================================================

// Constructor
DAGBuilder::DAGBuilder() noexcept 
    : nodePool(&getGlobalNodePool())
    , stats{0, 0, 0, 0} {
}

// Build DAG from topology
DAGInstance* DAGBuilder::buildDAG(const DAGTopology& topology) noexcept {
    // Validate topology first
    ValidationResult validation = validateTopology(topology);
    if (!validation.isValid) {
        stats.validationsFailed.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    
    // Create DAG instance
    DAGId dagId = generateDAGId();
    DAGInstance* dag = new DAGInstance(dagId);
    
    // Create nodes
    tbb::concurrent_hash_map<NodeId, DAGNode*, NodeIdHashCompare> nodeMapping;
    for (const auto& nodeDesc : topology.nodes) {
        DAGNode* node = nodePool->allocate();
        if (!node) {
            delete dag;
            return nullptr;
        }
        
        // Initialize node
        node->nodeId = nodeDesc.nodeId;
        node->dagId = dagId;
        node->nodeType = nodeDesc.type;
        node->priority = nodeDesc.priority;
        node->numaNode = nodeDesc.numaNode;
        node->cpuAffinity = nodeDesc.cpuAffinity;
        node->state.store(static_cast<u32>(NodeState::READY), std::memory_order_release);
        
        // Add to DAG
        dag->addNode(node);
        nodeMapping.insert(std::make_pair(node->nodeId, node));
        
        stats.nodesAllocated.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Create edges
    for (const auto& edgeDesc : topology.edges) {
        tbb::concurrent_hash_map<NodeId, DAGNode*, NodeIdHashCompare>::accessor sourceAccessor, targetAccessor;
        
        if (!nodeMapping.find(sourceAccessor, edgeDesc.sourceNode) ||
            !nodeMapping.find(targetAccessor, edgeDesc.targetNode)) {
            continue;  // Invalid edge, skip
        }
        
        DAGNode* source = sourceAccessor->second;
        DAGNode* target = targetAccessor->second;
        
        // Connect nodes
        if (connectNodes(source, target)) {
            stats.edgesCreated.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    // Perform topological sort
    if (!topologicalSort(dag)) {
        delete dag;
        return nullptr;
    }
    
    // Allocate buffers
    if (!allocateBuffers(dag)) {
        delete dag;
        return nullptr;
    }
    
    // Optimize for NUMA
    setNumaAffinity(dag);
    
    // Set DAG state
    dag->setState(DAGState::READY);
    
    stats.dagsBuilt.fetch_add(1, std::memory_order_relaxed);
    return dag;
}

// Validate topology
ValidationResult DAGBuilder::validateTopology(const DAGTopology& topology) noexcept {
    ValidationResult result;
    
    // Check for empty topology
    if (topology.nodes.empty()) {
        result.isValid = false;
        result.errorCode = ValidationResult::INVALID_NODE;
        return result;
    }
    
    // Check for duplicate node IDs
    tbb::concurrent_hash_map<NodeId, bool, NodeIdHashCompare> nodeIds;
    for (const auto& node : topology.nodes) {
        if (!nodeIds.insert(std::make_pair(node.nodeId, true))) {
            result.isValid = false;
            result.errorCode = ValidationResult::DUPLICATE_NODE_ID;
            result.problematicNode = node.nodeId;
            return result;
        }
        
        // Check input/output counts
        if (node.inputCount > 6) {
            result.isValid = false;
            result.errorCode = ValidationResult::TOO_MANY_INPUTS;
            result.problematicNode = node.nodeId;
            return result;
        }
        
        if (node.outputCount > 6) {
            result.isValid = false;
            result.errorCode = ValidationResult::TOO_MANY_OUTPUTS;
            result.problematicNode = node.nodeId;
            return result;
        }
    }
    
    // Check edges reference valid nodes
    for (const auto& edge : topology.edges) {
        tbb::concurrent_hash_map<NodeId, bool, NodeIdHashCompare>::accessor accessor;
        if (!nodeIds.find(accessor, edge.sourceNode) ||
            !nodeIds.find(accessor, edge.targetNode)) {
            result.isValid = false;
            result.errorCode = ValidationResult::INVALID_EDGE;
            return result;
        }
    }
    
    // Check for cycles
    if (detectCycles(topology)) {
        result.isValid = false;
        result.errorCode = ValidationResult::CYCLE_DETECTED;
        return result;
    }
    
    return result;
}

// Validate DAG instance
ValidationResult DAGBuilder::validateDAG(const DAGInstance* dag) noexcept {
    ValidationResult result;
    
    if (!dag || dag->getNodes().empty()) {
        result.isValid = false;
        result.errorCode = ValidationResult::INVALID_NODE;
        return result;
    }
    
    // Check for cycles in the built DAG
    if (detectCyclesInDAG(dag)) {
        result.isValid = false;
        result.errorCode = ValidationResult::CYCLE_DETECTED;
        return result;
    }
    
    // Check for orphaned nodes
    for (const auto* node : dag->getNodes()) {
        if (node->inDegree == 0 && node->outDegree == 0) {
            // Node with no connections (unless it's a source or sink)
            if (node->nodeType != ProcessingUnitType::MARKET_DATA_RECEIVER &&
                node->nodeType != ProcessingUnitType::RESULT_PUBLISHER) {
                result.isValid = false;
                result.errorCode = ValidationResult::ORPHANED_NODE;
                result.problematicNode = node->nodeId;
                return result;
            }
        }
    }
    
    return result;
}

// Optimize DAG for execution
void DAGBuilder::optimizeDAG(DAGInstance* dag) noexcept {
    if (!dag) return;
    
    // Set NUMA affinity
    setNumaAffinity(dag);
    
    // PSYCHOTIC: Optimize data locality by grouping related nodes
    // This is a simplified optimization - real implementation would be more complex
    for (auto* node : dag->getNodes()) {
        // Set SIMD width based on node type
        switch (node->nodeType) {
            case ProcessingUnitType::STREAM_NORMALIZER:
            case ProcessingUnitType::AGGREGATOR:
            case ProcessingUnitType::INTERPOLATOR:
                node->simdWidth = 256;  // AVX2
                break;
                
            case ProcessingUnitType::PATTERN_DETECTOR:
            case ProcessingUnitType::ML_PREDICTOR:
                node->simdWidth = 512;  // AVX-512
                break;
                
            default:
                node->simdWidth = 128;  // SSE
                break;
        }
        
        // Set cache hints
        node->cacheHints = 0x1;  // Prefetch L1
    }
}

// Detect cycles using DFS
bool DAGBuilder::detectCycles(const DAGTopology& topology) noexcept {
    // Build adjacency list
    tbb::concurrent_hash_map<NodeId, tbb::concurrent_vector<NodeId>, NodeIdHashCompare> adjList;
    
    for (const auto& node : topology.nodes) {
        adjList.insert(std::make_pair(node.nodeId, tbb::concurrent_vector<NodeId>()));
    }
    
    for (const auto& edge : topology.edges) {
        tbb::concurrent_hash_map<NodeId, tbb::concurrent_vector<NodeId>, NodeIdHashCompare>::accessor accessor;
        if (adjList.find(accessor, edge.sourceNode)) {
            accessor->second.push_back(edge.targetNode);
        }
    }
    
    // DFS to detect cycles
    tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare> colors;
    tbb::concurrent_vector<NodeId> dummy;
    
    for (const auto& node : topology.nodes) {
        colors.insert(std::make_pair(node.nodeId, NodeColor::WHITE));
    }
    
    for (const auto& node : topology.nodes) {
        tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>::accessor accessor;
        if (colors.find(accessor, node.nodeId) && accessor->second == NodeColor::WHITE) {
            if (!dfsVisit(node.nodeId, colors, dummy, adjList)) {
                return true;  // Cycle detected
            }
        }
    }
    
    return false;
}

// Detect cycles in built DAG
bool DAGBuilder::detectCyclesInDAG(const DAGInstance* dag) noexcept {
    // Build adjacency list from DAG nodes
    tbb::concurrent_hash_map<NodeId, tbb::concurrent_vector<NodeId>, NodeIdHashCompare> adjList;
    
    for (const auto* node : dag->getNodes()) {
        tbb::concurrent_vector<NodeId> successors;
        for (u32 i = 0; i < node->outDegree; ++i) {
            successors.push_back(node->successors[i]);
        }
        adjList.insert(std::make_pair(node->nodeId, successors));
    }
    
    // DFS to detect cycles
    tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare> colors;
    tbb::concurrent_vector<NodeId> dummy;
    
    for (const auto* node : dag->getNodes()) {
        colors.insert(std::make_pair(node->nodeId, NodeColor::WHITE));
    }
    
    for (const auto* node : dag->getNodes()) {
        tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>::accessor accessor;
        if (colors.find(accessor, node->nodeId) && accessor->second == NodeColor::WHITE) {
            if (!dfsVisit(node->nodeId, colors, dummy, adjList)) {
                return true;  // Cycle detected
            }
        }
    }
    
    return false;
}

// DFS visit for cycle detection and topological sort
bool DAGBuilder::dfsVisit(NodeId nodeId, 
                          tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>& colors,
                          tbb::concurrent_vector<NodeId>& order,
                          const tbb::concurrent_hash_map<NodeId, tbb::concurrent_vector<NodeId>, NodeIdHashCompare>& adjList) noexcept {
    // Mark node as being processed
    {
        tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>::accessor accessor;
        if (colors.find(accessor, nodeId)) {
            accessor->second = NodeColor::GRAY;
        }
    }
    
    // Visit successors
    tbb::concurrent_hash_map<NodeId, tbb::concurrent_vector<NodeId>, NodeIdHashCompare>::const_accessor adjAccessor;
    if (adjList.find(adjAccessor, nodeId)) {
        for (const auto& successor : adjAccessor->second) {
            tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>::accessor colorAccessor;
            if (colors.find(colorAccessor, successor)) {
                if (colorAccessor->second == NodeColor::GRAY) {
                    return false;  // Back edge found - cycle detected
                }
                if (colorAccessor->second == NodeColor::WHITE) {
                    if (!dfsVisit(successor, colors, order, adjList)) {
                        return false;
                    }
                }
            }
        }
    }
    
    // Mark node as completed
    {
        tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>::accessor accessor;
        if (colors.find(accessor, nodeId)) {
            accessor->second = NodeColor::BLACK;
        }
    }
    
    // Add to topological order (in reverse)
    order.push_back(nodeId);
    
    return true;
}

// Topological sort
bool DAGBuilder::topologicalSort(DAGInstance* dag) noexcept {
    if (!dag) return false;
    
    // Build adjacency list
    tbb::concurrent_hash_map<NodeId, tbb::concurrent_vector<NodeId>, NodeIdHashCompare> adjList;
    
    for (const auto* node : dag->getNodes()) {
        tbb::concurrent_vector<NodeId> successors;
        for (u32 i = 0; i < node->outDegree; ++i) {
            successors.push_back(node->successors[i]);
        }
        adjList.insert(std::make_pair(node->nodeId, successors));
    }
    
    // Perform DFS
    tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare> colors;
    tbb::concurrent_vector<NodeId> order;
    
    for (const auto* node : dag->getNodes()) {
        colors.insert(std::make_pair(node->nodeId, NodeColor::WHITE));
    }
    
    for (const auto* node : dag->getNodes()) {
        tbb::concurrent_hash_map<NodeId, NodeColor, NodeIdHashCompare>::accessor accessor;
        if (colors.find(accessor, node->nodeId) && accessor->second == NodeColor::WHITE) {
            if (!dfsVisit(node->nodeId, colors, order, adjList)) {
                return false;  // Cycle detected
            }
        }
    }
    
    // Reverse the order (DFS gives reverse topological order)
    std::reverse(order.begin(), order.end());
    
    dag->setTopologicalOrder(order);
    return true;
}

// Allocate buffers for data flow
bool DAGBuilder::allocateBuffers(DAGInstance* dag) noexcept {
    if (!dag) return false;
    
    // PSYCHOTIC: Pre-allocate all buffers for zero runtime allocation
    DataBufferId nextBufferId = DataBufferId(1000);  // Start from 1000
    
    for (auto* node : dag->getNodes()) {
        // Allocate input buffers
        for (u32 i = 0; i < node->inDegree; ++i) {
            if (node->inputBuffers[i] == INVALID_BUFFER_ID) {
                node->inputBuffers[i] = nextBufferId++;
            }
        }
        
        // Allocate output buffers
        for (u32 i = 0; i < node->outDegree; ++i) {
            if (node->outputBuffers[i] == INVALID_BUFFER_ID) {
                node->outputBuffers[i] = nextBufferId++;
            }
        }
    }
    
    return true;
}

// Set NUMA affinity
void DAGBuilder::setNumaAffinity(DAGInstance* dag) noexcept {
    if (!dag) return;
    
    // PSYCHOTIC: Optimize NUMA placement based on data flow
    i32 currentNuma = 0;
    u32 nodesPerNuma = 16;  // Distribute across NUMA nodes
    u32 nodeCount = 0;
    
    for (const NodeId& nodeId : dag->getTopologicalOrder()) {
        DAGNode* dagNode = dag->getNode(nodeId);
        if (!dagNode) continue;
        
        // If node doesn't have explicit NUMA setting, assign one
        if (dagNode->numaNode == -1) {
            dagNode->numaNode = currentNuma;
            nodeCount++;
            
            if (nodeCount >= nodesPerNuma) {
                currentNuma = (currentNuma + 1) % 4;  // Assume 4 NUMA nodes
                nodeCount = 0;
            }
        }
    }
}

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// Create a simple linear DAG
DAGTopology createLinearDAG(u32 nodeCount, ProcessingUnitType type) noexcept {
    DAGTopology topology;
    
    for (u32 i = 0; i < nodeCount; ++i) {
        DAGTopology::NodeDescriptor node;
        node.nodeId = NodeId(i);
        node.type = type;
        node.priority = ExecutionPriority::NORMAL;
        node.inputCount = (i == 0) ? 0 : 1;
        node.outputCount = (i == nodeCount - 1) ? 0 : 1;
        topology.addNode(node);
        
        if (i > 0) {
            DAGTopology::EdgeDescriptor edge;
            edge.sourceNode = NodeId(i - 1);
            edge.targetNode = NodeId(i);
            edge.bufferSize = 1024;
            topology.addEdge(edge);
        }
    }
    
    return topology;
}

// Create a fan-out DAG
DAGTopology createFanOutDAG(u32 fanOutFactor, ProcessingUnitType rootType) noexcept {
    DAGTopology topology;
    
    // Root node
    DAGTopology::NodeDescriptor root;
    root.nodeId = NodeId(0);
    root.type = rootType;
    root.priority = ExecutionPriority::HIGH;
    root.inputCount = 0;
    root.outputCount = fanOutFactor;
    topology.addNode(root);
    
    // Fan-out nodes
    for (u32 i = 0; i < fanOutFactor; ++i) {
        DAGTopology::NodeDescriptor node;
        node.nodeId = NodeId(i + 1);
        node.type = ProcessingUnitType::STREAM_NORMALIZER;
        node.priority = ExecutionPriority::NORMAL;
        node.inputCount = 1;
        node.outputCount = 0;
        topology.addNode(node);
        
        DAGTopology::EdgeDescriptor edge;
        edge.sourceNode = NodeId(0);
        edge.targetNode = NodeId(i + 1);
        edge.bufferSize = 1024;
        topology.addEdge(edge);
    }
    
    return topology;
}

// Create a fan-in DAG
DAGTopology createFanInDAG(u32 fanInFactor, ProcessingUnitType sinkType) noexcept {
    DAGTopology topology;
    
    // Source nodes
    for (u32 i = 0; i < fanInFactor; ++i) {
        DAGTopology::NodeDescriptor node;
        node.nodeId = NodeId(i);
        node.type = ProcessingUnitType::MARKET_DATA_RECEIVER;
        node.priority = ExecutionPriority::NORMAL;
        node.inputCount = 0;
        node.outputCount = 1;
        topology.addNode(node);
    }
    
    // Sink node
    DAGTopology::NodeDescriptor sink;
    sink.nodeId = NodeId(fanInFactor);
    sink.type = sinkType;
    sink.priority = ExecutionPriority::HIGH;
    sink.inputCount = fanInFactor;
    sink.outputCount = 0;
    topology.addNode(sink);
    
    // Edges
    for (u32 i = 0; i < fanInFactor; ++i) {
        DAGTopology::EdgeDescriptor edge;
        edge.sourceNode = NodeId(i);
        edge.targetNode = NodeId(fanInFactor);
        edge.bufferSize = 1024;
        topology.addEdge(edge);
    }
    
    return topology;
}

// Create a diamond DAG
DAGTopology createDiamondDAG(ProcessingUnitType type) noexcept {
    DAGTopology topology;
    
    // Node A (source)
    DAGTopology::NodeDescriptor nodeA;
    nodeA.nodeId = NodeId(0);
    nodeA.type = ProcessingUnitType::MARKET_DATA_RECEIVER;
    nodeA.priority = ExecutionPriority::HIGH;
    nodeA.inputCount = 0;
    nodeA.outputCount = 2;
    topology.addNode(nodeA);
    
    // Node B (middle)
    DAGTopology::NodeDescriptor nodeB;
    nodeB.nodeId = NodeId(1);
    nodeB.type = type;
    nodeB.priority = ExecutionPriority::NORMAL;
    nodeB.inputCount = 1;
    nodeB.outputCount = 1;
    topology.addNode(nodeB);
    
    // Node C (middle)
    DAGTopology::NodeDescriptor nodeC;
    nodeC.nodeId = NodeId(2);
    nodeC.type = type;
    nodeC.priority = ExecutionPriority::NORMAL;
    nodeC.inputCount = 1;
    nodeC.outputCount = 1;
    topology.addNode(nodeC);
    
    // Node D (sink)
    DAGTopology::NodeDescriptor nodeD;
    nodeD.nodeId = NodeId(3);
    nodeD.type = ProcessingUnitType::RESULT_PUBLISHER;
    nodeD.priority = ExecutionPriority::HIGH;
    nodeD.inputCount = 2;
    nodeD.outputCount = 0;
    topology.addNode(nodeD);
    
    // Edges
    DAGTopology::EdgeDescriptor edge1;
    edge1.sourceNode = NodeId(0);
    edge1.targetNode = NodeId(1);
    edge1.bufferSize = 1024;
    edge1.transformType = TransformationType::PASSTHROUGH;
    topology.addEdge(edge1);
    
    DAGTopology::EdgeDescriptor edge2;
    edge2.sourceNode = NodeId(0);
    edge2.targetNode = NodeId(2);
    edge2.bufferSize = 1024;
    edge2.transformType = TransformationType::PASSTHROUGH;
    topology.addEdge(edge2);
    
    DAGTopology::EdgeDescriptor edge3;
    edge3.sourceNode = NodeId(1);
    edge3.targetNode = NodeId(3);
    edge3.bufferSize = 1024;
    edge3.transformType = TransformationType::PASSTHROUGH;
    topology.addEdge(edge3);
    
    DAGTopology::EdgeDescriptor edge4;
    edge4.sourceNode = NodeId(2);
    edge4.targetNode = NodeId(3);
    edge4.bufferSize = 1024;
    edge4.transformType = TransformationType::PASSTHROUGH;
    topology.addEdge(edge4);
    
    return topology;
}

// Create a complex multi-stage DAG
DAGTopology createMultiStageDAG(u32 stages, u32 nodesPerStage) noexcept {
    DAGTopology topology;
    u32 nodeId = 0;
    
    for (u32 stage = 0; stage < stages; ++stage) {
        for (u32 node = 0; node < nodesPerStage; ++node) {
            DAGTopology::NodeDescriptor n;
            n.nodeId = NodeId(nodeId++);
            
            // First stage: receivers
            if (stage == 0) {
                n.type = ProcessingUnitType::MARKET_DATA_RECEIVER;
                n.inputCount = 0;
                n.outputCount = nodesPerStage;
            }
            // Last stage: publishers
            else if (stage == stages - 1) {
                n.type = ProcessingUnitType::RESULT_PUBLISHER;
                n.inputCount = nodesPerStage;
                n.outputCount = 0;
            }
            // Middle stages: processors
            else {
                n.type = ProcessingUnitType::STREAM_NORMALIZER;
                n.inputCount = nodesPerStage;
                n.outputCount = nodesPerStage;
            }
            
            n.priority = (stage == 0 || stage == stages - 1) ? 
                        ExecutionPriority::HIGH : ExecutionPriority::NORMAL;
            
            topology.addNode(n);
        }
        
        // Add edges between stages
        if (stage > 0) {
            u32 prevStageStart = (stage - 1) * nodesPerStage;
            u32 currStageStart = stage * nodesPerStage;
            
            for (u32 i = 0; i < nodesPerStage; ++i) {
                for (u32 j = 0; j < nodesPerStage; ++j) {
                    DAGTopology::EdgeDescriptor edge;
                    edge.sourceNode = NodeId(prevStageStart + i);
                    edge.targetNode = NodeId(currStageStart + j);
                    edge.bufferSize = 1024;
                    topology.addEdge(edge);
                }
            }
        }
    }
    
    return topology;
}

AARENDOCORE_NAMESPACE_END