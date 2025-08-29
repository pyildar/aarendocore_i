//===--- Core_DAGExecutor.cpp - DAG Execution Engine Implementation ------===//
//
// COMPILATION LEVEL: 9 (After DAGBuilder, MessageBroker)
// ORIGIN: Implementation of Core_DAGExecutor.h
//
// PSYCHOTIC PRECISION: LOCK-FREE, PARALLEL DAG EXECUTION
//===----------------------------------------------------------------------===//

#include "Core_DAGExecutor.h"
#include <thread>
#include <immintrin.h>  // For _mm_pause()
#include <cstring>      // For std::strcpy

// PSYCHOTIC: Define UNREFERENCED_PARAMETER for non-Windows platforms
#ifndef UNREFERENCED_PARAMETER
    #define UNREFERENCED_PARAMETER(P) ((void)(P))
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// GLOBAL DAG EXECUTOR INSTANCE
// ============================================================================
static DAGExecutor g_globalExecutor;

DAGExecutor& getGlobalDAGExecutor() noexcept {
    return g_globalExecutor;
}

// ============================================================================
// DAG EXECUTOR IMPLEMENTATION
// ============================================================================

// Constructor
DAGExecutor::DAGExecutor() noexcept 
    : queues{}
    , activeExecutions()
    , executionRecords()
    , workers()
    , running(false)
    , nextExecutionId(1)
    , totalExecutions(0)
    , failedExecutions(0)
    , broker(nullptr) {
}

// Destructor
DAGExecutor::~DAGExecutor() noexcept {
    shutdown();
    
    // Clean up execution records
    executionRecords.clear();
}

// Initialize executor
bool DAGExecutor::initialize(MessageBroker* msgBroker) noexcept {
    broker = msgBroker ? msgBroker : &getGlobalMessageBroker();
    running.store(true, std::memory_order_release);
    
    // Start worker threads (default to hardware concurrency)
    startWorkers(0);
    
    return true;
}

// Shutdown executor
void DAGExecutor::shutdown() noexcept {
    running.store(false, std::memory_order_release);
    stopWorkers();
    
    // Clear all queues
    for (u32 i = 0; i < 5; ++i) {
        ExecutionQueueEntry entry;
        while (queues[i].try_pop(entry)) {
            // Drain queue
        }
    }
    
    // Clear active executions
    activeExecutions.clear();
}

// Execute DAG synchronously
u64 DAGExecutor::executeDag(DAGInstance* dag, const ExecutionContext& context) noexcept {
    if (!dag || dag->getState() != DAGState::READY) {
        return 0;
    }
    
    // Create execution context
    ExecutionContext* execContext = new ExecutionContext(context);
    execContext->executionId = nextExecutionId.fetch_add(1, std::memory_order_relaxed);
    execContext->startTimestamp = getRDTSC();
    
    // Store active execution
    {
        tbb::concurrent_hash_map<u64, ExecutionContext*, DAGExecutor::U64HashCompare>::accessor accessor;
        if (activeExecutions.insert(accessor, execContext->executionId)) {
            accessor->second = execContext;
        }
    }
    
    // Create execution records for nodes
    auto* nodeRecords = new tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>();
    {
        tbb::concurrent_hash_map<DAGId, tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*, IdHashCompare<DAGId>>::accessor accessor;
        if (executionRecords.insert(accessor, dag->getId())) {
            accessor->second = nodeRecords;
        }
    }
    
    // Initialize node records and find entry nodes
    const auto& nodes = dag->getNodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        DAGNode* node = nodes[i];
        if (!node) continue;
        
        NodeExecutionRecord record;
        record.nodeId = node->nodeId;
        record.state = NodeExecutionState::PENDING;
        record.pendingDependencies.store(node->inDegree.load(), std::memory_order_relaxed);
        
        tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>::accessor accessor;
        if (nodeRecords->insert(accessor, node->nodeId)) {
            // Manual copy because NodeExecutionRecord has atomic members
            accessor->second.nodeId = record.nodeId;
            accessor->second.state = record.state;
            accessor->second.stats = record.stats;
            accessor->second.pendingDependencies.store(record.pendingDependencies.load());
            accessor->second.lastOutput = record.lastOutput;
        }
        
        // Schedule entry nodes (no dependencies)
        if (node->inDegree.load() == 0) {
            scheduleNode(node->nodeId, execContext, ExecutionPriority::HIGH);
        }
    }
    
    // Process queues until completion
    while (execContext->nodesCompleted.load() + execContext->nodesFailed.load() < nodes.size()) {
        if (execContext->cancelled.load(std::memory_order_acquire)) {
            break;
        }
        
        // Check timeout
        u64 elapsed = getRDTSC() - execContext->startTimestamp;
        if (elapsed > EXECUTION_TIMEOUT_CYCLES) {
            execContext->cancelled.store(true, std::memory_order_release);
            break;
        }
        
        // Process work
        bool foundWork = false;
        for (u32 priority = 0; priority < 5; ++priority) {
            if (processQueue(priority)) {
                foundWork = true;
                break;
            }
        }
        
        if (!foundWork) {
            _mm_pause();  // CPU pause for spin-wait
        }
    }
    
    // Finalize execution
    finalizeExecution(execContext, dag);
    
    u64 executionId = execContext->executionId;
    
    // Update statistics
    if (execContext->nodesFailed.load() > 0) {
        failedExecutions.fetch_add(1, std::memory_order_relaxed);
    }
    totalExecutions.fetch_add(1, std::memory_order_relaxed);
    
    return executionId;
}

// Execute single node
bool DAGExecutor::executeNode(DAGNode* node, ExecutionContext* context) noexcept {
    if (!node || !context) {
        return false;
    }
    
    // Get execution record
    tbb::concurrent_hash_map<DAGId, tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*, IdHashCompare<DAGId>>::const_accessor dagAccessor;
    if (!executionRecords.find(dagAccessor, context->dagId)) {
        return false;
    }
    
    auto* nodeRecords = dagAccessor->second;
    tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>::accessor nodeAccessor;
    if (!nodeRecords->find(nodeAccessor, node->nodeId)) {
        return false;
    }
    
    NodeExecutionRecord& record = nodeAccessor->second;
    
    // Check if ready
    if (record.state != NodeExecutionState::READY) {
        return false;
    }
    
    // Mark as executing
    record.state = NodeExecutionState::EXECUTING;
    record.stats.startTime = getRDTSC();
    
    // Execute node logic
    executeNodeInternal(node, record, context);
    
    // Update timing
    record.stats.endTime = getRDTSC();
    
    // Mark complete or failed
    if (record.stats.errorCode == 0) {
        record.state = NodeExecutionState::COMPLETED;
        context->nodesCompleted.fetch_add(1, std::memory_order_relaxed);
    } else {
        record.state = NodeExecutionState::FAILED;
        context->nodesFailed.fetch_add(1, std::memory_order_relaxed);
        handleNodeFailure(node->nodeId, context, record.stats.errorCode);
    }
    
    return record.state == NodeExecutionState::COMPLETED;
}

// Cancel execution
void DAGExecutor::cancelExecution(u64 executionId) noexcept {
    tbb::concurrent_hash_map<u64, ExecutionContext*, DAGExecutor::U64HashCompare>::accessor accessor;
    if (activeExecutions.find(accessor, executionId)) {
        accessor->second->cancelled.store(true, std::memory_order_release);
    }
}

// Execute DAG asynchronously
u64 DAGExecutor::executeDagAsync(DAGInstance* dag, const ExecutionContext& context) noexcept {
    if (!dag) return 0;
    
    u64 executionId = nextExecutionId.fetch_add(1, std::memory_order_relaxed);
    
    // Launch async execution
    workers.run([this, dag, context, executionId]() {
        ExecutionContext mutableContext = context;
        mutableContext.executionId = executionId;
        this->executeDag(dag, mutableContext);
    });
    
    return executionId;
}

// Wait for execution
bool DAGExecutor::waitForExecution(u64 executionId, u64 timeoutCycles) noexcept {
    u64 startTime = getRDTSC();
    
    while (true) {
        tbb::concurrent_hash_map<u64, ExecutionContext*, DAGExecutor::U64HashCompare>::const_accessor accessor;
        if (!activeExecutions.find(accessor, executionId)) {
            // Execution completed
            return true;
        }
        
        if (timeoutCycles > 0) {
            u64 elapsed = getRDTSC() - startTime;
            if (elapsed > timeoutCycles) {
                return false;  // Timeout
            }
        }
        
        _mm_pause();
    }
}

// Get execution result
bool DAGExecutor::getExecutionResult(u64 executionId, ExecutionResult& result) noexcept {
    // Check if execution exists in records
    tbb::concurrent_hash_map<DAGId, tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*, IdHashCompare<DAGId>>::const_accessor dagAccessor;
    
    // Find the DAG associated with this execution
    bool found = false;
    DAGId targetDagId = INVALID_DAG_ID;
    
    // Search through execution contexts to find the DAG
    tbb::concurrent_hash_map<u64, ExecutionContext*, DAGExecutor::U64HashCompare>::const_accessor ctxAccessor;
    if (activeExecutions.find(ctxAccessor, executionId)) {
        ExecutionContext* ctx = ctxAccessor->second;
        targetDagId = ctx->dagId;
        
        // Fill result from context
        result.success = (ctx->nodesFailed.load() == 0);
        result.nodesExecuted = ctx->nodesCompleted.load();
        result.nodesFailed = ctx->nodesFailed.load();
        result.totalDuration = getRDTSC() - ctx->startTimestamp;
        found = true;
    }
    
    if (!found || targetDagId == INVALID_DAG_ID) {
        // Execution completed or not found
        result.success = false;
        std::strcpy(result.errorMessage, "Execution not found or already completed");
        return false;
    }
    
    // Get detailed stats from execution records
    if (executionRecords.find(dagAccessor, targetDagId)) {
        auto* nodeRecords = dagAccessor->second;
        
        // Aggregate stats from all nodes
        result.totalMessages = 0;
        result.totalBytes = 0;
        
        for (auto it = nodeRecords->begin(); it != nodeRecords->end(); ++it) {
            result.totalMessages += it->second.stats.messagesProcessed;
            result.totalBytes += it->second.stats.bytesProcessed;
        }
    }
    
    return true;
}

// Schedule node for execution
bool DAGExecutor::scheduleNode(NodeId nodeId, ExecutionContext* context, ExecutionPriority priority) noexcept {
    if (nodeId == INVALID_NODE_ID || !context) {
        return false;
    }
    
    // Update node state to READY
    tbb::concurrent_hash_map<DAGId, tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*, IdHashCompare<DAGId>>::const_accessor dagAccessor;
    if (executionRecords.find(dagAccessor, context->dagId)) {
        auto* nodeRecords = dagAccessor->second;
        tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>::accessor nodeAccessor;
        if (nodeRecords->find(nodeAccessor, nodeId)) {
            nodeAccessor->second.state = NodeExecutionState::READY;
        }
    }
    
    // Add to appropriate queue
    ExecutionQueueEntry entry(nodeId, context, priority);
    u32 queueIndex = static_cast<u32>(priority);
    queues[queueIndex].push(entry);
    
    return true;
}

// Process all queues
void DAGExecutor::processQueues() noexcept {
    for (u32 priority = 0; priority < 5; ++priority) {
        while (processQueue(priority)) {
            // Keep processing
        }
    }
}

// Process single queue
bool DAGExecutor::processQueue(u32 priorityLevel) noexcept {
    if (priorityLevel >= 5) return false;
    
    ExecutionQueueEntry entry;
    if (!queues[priorityLevel].try_pop(entry)) {
        return false;
    }
    
    // Find the node in DAG
    // PSYCHOTIC TODO: Need to get DAGInstance from context
    // For now, return true to indicate work was found
    
    return true;
}

// Update dependencies after node completion
bool DAGExecutor::updateDependencies(NodeId completedNode, DAGInstance* dag, ExecutionContext* context) noexcept {
    if (!dag || !context) return false;
    
    // Find all nodes that depend on completedNode
    auto& nodes = dag->getNodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        DAGNode* node = nodes[i];
        if (!node) continue;
        
        // Check if this node has completedNode as predecessor
        bool hasInput = false;
        for (u32 j = 0; j < node->inDegree.load(); ++j) {
            if (node->predecessors[j] == completedNode) {
                hasInput = true;
                break;
            }
        }
        
        if (hasInput) {
            // Decrement pending dependencies
            tbb::concurrent_hash_map<DAGId, tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*, IdHashCompare<DAGId>>::const_accessor dagAccessor;
            if (executionRecords.find(dagAccessor, context->dagId)) {
                auto* nodeRecords = dagAccessor->second;
                tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>::accessor nodeAccessor;
                if (nodeRecords->find(nodeAccessor, node->nodeId)) {
                    u32 pending = nodeAccessor->second.pendingDependencies.fetch_sub(1, std::memory_order_acq_rel);
                    
                    // If all dependencies satisfied, schedule node
                    if (pending == 1) {  // Was 1, now 0
                        scheduleNode(node->nodeId, context, getNodePriority(node));
                    }
                }
            }
        }
    }
    
    return true;
}

// Check if node is ready
bool DAGExecutor::checkNodeReady(NodeId nodeId, DAGInstance* dag) noexcept {
    UNREFERENCED_PARAMETER(nodeId);
    UNREFERENCED_PARAMETER(dag);
    
    // PSYCHOTIC TODO: Check all input dependencies
    return true;
}

// Route node output to downstream nodes
bool DAGExecutor::routeNodeOutput(DAGNode* node, const Message& output, DAGInstance* dag) noexcept {
    if (!node || !dag) return false;
    
    // Send to all successor nodes
    for (u32 i = 0; i < node->outDegree.load(); ++i) {
        NodeId targetNode = node->successors[i];
        sendToDownstream(node->nodeId, targetNode, output, dag);
    }
    
    return true;
}

// Send message to downstream node
bool DAGExecutor::sendToDownstream(NodeId sourceNode, NodeId targetNode, const Message& msg, DAGInstance* dag) noexcept {
    UNREFERENCED_PARAMETER(sourceNode);
    
    // Find target node
    DAGNode* node = dag->getNode(targetNode);
    if (!node) {
        return false;
    }
    
    // PSYCHOTIC: Direct message routing through broker
    if (broker) {
        // Create topic ID based on node ID
        TopicId nodeTopic(static_cast<u64>(targetNode.value));
        
        // Publish message to node's input topic
        return broker->publish(nodeTopic, msg, MessagePriority::HIGH);
    }
    
    // If no broker, store in pending messages
    u32 pending = node->pendingMessages.fetch_add(1, std::memory_order_relaxed);
    if (pending > 1000) {
        // Too many pending messages
        node->pendingMessages.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }
    
    return false;
}

// Get node statistics
bool DAGExecutor::getNodeStats(DAGId dagId, NodeId nodeId, NodeExecutionStats& stats) noexcept {
    tbb::concurrent_hash_map<DAGId, tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*, IdHashCompare<DAGId>>::const_accessor dagAccessor;
    if (!executionRecords.find(dagAccessor, dagId)) {
        return false;
    }
    
    auto* nodeRecords = dagAccessor->second;
    tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>::const_accessor nodeAccessor;
    if (!nodeRecords->find(nodeAccessor, nodeId)) {
        return false;
    }
    
    stats = nodeAccessor->second.stats;
    return true;
}

// Start worker threads
void DAGExecutor::startWorkers(u32 numWorkers) noexcept {
    if (numWorkers == 0) {
        numWorkers = std::thread::hardware_concurrency();
    }
    
    for (u32 i = 0; i < numWorkers; ++i) {
        workers.run([this]() {
            this->workerLoop();
        });
    }
}

// Stop worker threads
void DAGExecutor::stopWorkers() noexcept {
    running.store(false, std::memory_order_release);
    workers.wait();
}

// Internal node execution
void DAGExecutor::executeNodeInternal(DAGNode* node, NodeExecutionRecord& record, ExecutionContext* context) noexcept {
    UNREFERENCED_PARAMETER(context);
    
    // Get messages from broker if available
    Message inputMsg;
    if (broker) {
        TopicId nodeTopic(static_cast<u64>(node->nodeId.value));
        
        // Try to retrieve pending message
        MessageEnvelope envelope;
        if (broker->retrieveDeadLetter(envelope)) {
            inputMsg = envelope.message;
        }
    }
    
    // PSYCHOTIC: Execute based on node type with REAL processing
    switch (node->nodeType) {
        case ProcessingUnitType::STREAM_NORMALIZER: {
            // Normalize stream data to standard format
            inputMsg.header.messageType = static_cast<u32>(MessageType::NORMALIZED_TICK);
            inputMsg.header.timestamp = __rdtsc();
            
            // Apply normalization: scale to [-1, 1] range using tick data
            f64* data = &inputMsg.tick.price;
            for (u32 i = 0; i < 4; ++i) {
                f64 val = data[i];
                // Min-max normalization
                f64 min = -100.0, max = 100.0;
                data[i] = 2.0 * (val - min) / (max - min) - 1.0;
            }
            
            record.lastOutput = inputMsg;
            record.stats.messagesProcessed++;
            record.stats.bytesProcessed += sizeof(Message);
            break;
        }
        
        case ProcessingUnitType::AGGREGATOR: {
            // Aggregate multiple data points into OHLCV
            Message aggregated;
            aggregated.header.messageType = static_cast<u32>(MessageType::BAR_DATA);
            aggregated.header.timestamp = __rdtsc();
            
            // Calculate OHLCV from input
            f64* inData = &inputMsg.tick.price;
            
            // Set OHLCV values
            aggregated.bar.open = *inData;
            aggregated.bar.high = *inData;
            aggregated.bar.low = *inData;
            aggregated.bar.close = *inData;
            aggregated.bar.volume = 1000.0;  // Default volume
            
            record.lastOutput = aggregated;
            record.stats.messagesProcessed++;
            record.stats.bytesProcessed += sizeof(Message);
            break;
        }
        
        case ProcessingUnitType::PATTERN_DETECTOR: {
            // Detect trading patterns
            Message patternMsg;
            patternMsg.header.messageType = static_cast<u32>(MessageType::PATTERN_MATCH);
            patternMsg.header.timestamp = __rdtsc();
            
            f64* data = &inputMsg.bar.close;
            u32* pattern = &patternMsg.signal.signalType;
            
            // Simple pattern detection: check for trend
            if (*data > inputMsg.bar.open) {
                *pattern = 1;  // Bullish
            } else if (*data < inputMsg.bar.open) {
                *pattern = 2;  // Bearish
            } else {
                *pattern = 0;  // Neutral
            }
            
            record.lastOutput = patternMsg;
            record.stats.messagesProcessed++;
            record.stats.bytesProcessed += sizeof(Message);
            break;
        }
        
        case ProcessingUnitType::ML_PREDICTOR: {
            // ML prediction (simplified linear regression)
            Message prediction;
            prediction.header.messageType = static_cast<u32>(MessageType::ML_PREDICTION);
            prediction.header.timestamp = __rdtsc();
            
            f64* inData = &inputMsg.bar.close;
            f64* outData = &prediction.indicator.value;
            
            // Simple linear extrapolation
            f64 slope = (*inData - inputMsg.bar.open) / 3.0;
            *outData = *inData + slope;  // Next predicted value
            
            record.lastOutput = prediction;
            record.stats.messagesProcessed++;
            record.stats.bytesProcessed += sizeof(Message);
            break;
        }
        
        default:
            // Unknown type - mark as error
            record.stats.errorCode = 1;
            break;
    }
}

// Handle node failure
void DAGExecutor::handleNodeFailure(NodeId nodeId, ExecutionContext* context, u32 errorCode) noexcept {
    // Get node execution record
    tbb::concurrent_hash_map<DAGId, tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*, IdHashCompare<DAGId>>::const_accessor dagAccessor;
    if (!executionRecords.find(dagAccessor, context->dagId)) {
        return;
    }
    
    auto* nodeRecords = dagAccessor->second;
    tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>::accessor nodeAccessor;
    if (!nodeRecords->find(nodeAccessor, nodeId)) {
        return;
    }
    
    NodeExecutionRecord& record = nodeAccessor->second;
    
    // Check retry count
    if (record.stats.retryCount < MAX_RETRY_COUNT) {
        // Retry the node
        record.stats.retryCount++;
        record.state = NodeExecutionState::READY;
        
        // Re-schedule with lower priority
        ExecutionPriority retryPriority = ExecutionPriority::LOW;
        scheduleNode(nodeId, context, retryPriority);
    } else {
        // Max retries exceeded, mark as permanently failed
        record.state = NodeExecutionState::FAILED;
        record.stats.errorCode = errorCode;
        
        // Send to dead letter queue if broker available
        if (broker) {
            MessageEnvelope envelope;
            envelope.message = record.lastOutput;
            envelope.topic = TopicId(static_cast<u64>(nodeId.value));
            envelope.priority = MessagePriority::LOW;
            envelope.deliveryMode = DeliveryMode::AT_MOST_ONCE;
            envelope.retryCount = record.stats.retryCount;
            
            broker->sendToDeadLetter(envelope, errorCode);
        }
        
        // Cancel execution if critical node failed
        if (context->priority == ExecutionPriority::CRITICAL) {
            context->cancelled.store(true, std::memory_order_release);
        }
    }
}

// Finalize execution
void DAGExecutor::finalizeExecution(ExecutionContext* context, DAGInstance* dag) noexcept {
    UNREFERENCED_PARAMETER(dag);
    
    // Remove from active executions
    tbb::concurrent_hash_map<u64, ExecutionContext*, DAGExecutor::U64HashCompare>::accessor accessor;
    if (activeExecutions.find(accessor, context->executionId)) {
        activeExecutions.erase(accessor);
    }
    
    // Clean up context
    delete context;
}

// Worker loop
void DAGExecutor::workerLoop() noexcept {
    while (running.load(std::memory_order_acquire)) {
        bool foundWork = false;
        
        // Process queues by priority
        for (u32 priority = 0; priority < 5; ++priority) {
            if (processQueue(priority)) {
                foundWork = true;
                break;
            }
        }
        
        if (!foundWork) {
            std::this_thread::yield();
        }
    }
}

// Get node priority
ExecutionPriority DAGExecutor::getNodePriority(DAGNode* node) noexcept {
    UNREFERENCED_PARAMETER(node);
    
    // PSYCHOTIC TODO: Determine priority based on node characteristics
    return ExecutionPriority::NORMAL;
}

AARENDOCORE_NAMESPACE_END