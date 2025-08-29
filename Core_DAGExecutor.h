//===--- Core_DAGExecutor.h - DAG Execution Engine ----------------------===//
//
// COMPILATION LEVEL: 9 (After DAGBuilder, MessageBroker)
// DEPENDENCIES:
//   - Core_Types.h (u32, u64, AtomicU64)
//   - Core_MessageTypes.h (Message)
//   - Core_DAGTypes.h (NodeId, DAGId, ExecutionMode)
//   - Core_DAGNode.h (DAGNode)
//   - Core_DAGBuilder.h (DAGInstance)
//   - Core_MessageBroker.h (MessageBroker)
// ORIGIN: NEW - DAG execution engine
//
// PSYCHOTIC PRECISION: LOCK-FREE, PARALLEL DAG EXECUTION
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_DAGEXECUTOR_H
#define AARENDOCORE_CORE_DAGEXECUTOR_H

#include "Core_Platform.h"
#include "Core_Types.h"
#include "Core_MessageTypes.h"
#include "Core_DAGTypes.h"
#include "Core_DAGNode.h"
#include "Core_DAGBuilder.h"
#include "Core_MessageBroker.h"
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>

AARENDOCORE_NAMESPACE_BEGIN

// ExecutionPriority already defined in Core_DAGTypes.h

// ============================================================================
// EXECUTION STATE - Node execution state
// ============================================================================
enum class NodeExecutionState : u32 {
    PENDING = 0,     // Not yet ready
    READY = 1,       // Ready to execute
    EXECUTING = 2,   // Currently executing
    COMPLETED = 3,   // Successfully completed
    FAILED = 4,      // Execution failed
    SKIPPED = 5      // Skipped due to dependencies
};

// ============================================================================
// NODE EXECUTION STATS - Per-node execution metrics
// ============================================================================
struct alignas(64) NodeExecutionStats {
    u64 startTime;        // RDTSC start
    u64 endTime;          // RDTSC end
    u64 messagesProcessed;
    u64 bytesProcessed;
    u32 retryCount;
    u32 errorCode;
    u32 reserved[6];      // Padding to 64 bytes
    
    NodeExecutionStats() noexcept 
        : startTime(0)
        , endTime(0)
        , messagesProcessed(0)
        , bytesProcessed(0)
        , retryCount(0)
        , errorCode(0)
        , reserved{} {}
};

// ============================================================================
// EXECUTION CONTEXT - Session context for execution
// ============================================================================
struct alignas(64) ExecutionContext {
    DAGId dagId;              // 8 bytes
    SessionId sessionId;      // 8 bytes
    u64 executionId;          // 8 bytes - Unique execution ID
    u64 startTimestamp;       // 8 bytes - RDTSC start
    AtomicU32 nodesCompleted; // 4 bytes
    AtomicU32 nodesFailed;    // 4 bytes
    ExecutionPriority priority; // 4 bytes
    u32 executionMode;         // 4 bytes - mode flags
    AtomicBool cancelled;      // 1 byte
    u8 padding[15];           // Padding to 64 bytes
    
    ExecutionContext() noexcept 
        : dagId(INVALID_DAG_ID)
        , sessionId()
        , executionId(0)
        , startTimestamp(0)
        , nodesCompleted(0)
        , nodesFailed(0)
        , priority(ExecutionPriority::NORMAL)
        , executionMode(0)  // Default mode
        , cancelled(false)
        , padding{} {}
        
    // Copy constructor - needed for atomics
    ExecutionContext(const ExecutionContext& other) noexcept 
        : dagId(other.dagId)
        , sessionId(other.sessionId)
        , executionId(other.executionId)
        , startTimestamp(other.startTimestamp)
        , nodesCompleted(other.nodesCompleted.load())
        , nodesFailed(other.nodesFailed.load())
        , priority(other.priority)
        , executionMode(other.executionMode)
        , cancelled(other.cancelled.load())
        , padding{} {}
};

// ============================================================================
// NODE EXECUTION RECORD - Tracks node execution
// ============================================================================
struct NodeExecutionRecord {
    NodeId nodeId;
    NodeExecutionState state;
    NodeExecutionStats stats;
    AtomicU32 pendingDependencies;
    Message lastOutput;  // Last message produced
    
    NodeExecutionRecord() noexcept 
        : nodeId(INVALID_NODE_ID)
        , state(NodeExecutionState::PENDING)
        , stats()
        , pendingDependencies(0)
        , lastOutput() {}
};

// ============================================================================
// EXECUTION QUEUE ENTRY - Work queue item
// ============================================================================
struct ExecutionQueueEntry {
    NodeId nodeId;
    ExecutionContext* context;
    ExecutionPriority priority;
    u64 scheduledTime;  // RDTSC when scheduled
    
    ExecutionQueueEntry() noexcept 
        : nodeId(INVALID_NODE_ID)
        , context(nullptr)
        , priority(ExecutionPriority::NORMAL)
        , scheduledTime(0) {}
        
    ExecutionQueueEntry(NodeId id, ExecutionContext* ctx, ExecutionPriority prio) noexcept
        : nodeId(id)
        , context(ctx)
        , priority(prio)
        , scheduledTime(__rdtsc()) {}
};

// ============================================================================
// EXECUTION RESULT - Result of DAG execution
// ============================================================================
struct ExecutionResult {
    bool success;
    u32 nodesExecuted;
    u32 nodesFailed;
    u64 totalDuration;  // RDTSC cycles
    u64 totalMessages;
    u64 totalBytes;
    char errorMessage[256];
    
    ExecutionResult() noexcept 
        : success(false)
        , nodesExecuted(0)
        , nodesFailed(0)
        , totalDuration(0)
        , totalMessages(0)
        , totalBytes(0)
        , errorMessage{} {}
};

// ============================================================================
// DAG EXECUTOR - Main execution engine
// ============================================================================
class DAGExecutor {
private:
    // Execution queues by priority - PSYCHOTIC: 5 priority levels!
    tbb::concurrent_queue<ExecutionQueueEntry> queues[5];
    
    // Hash compare for u64
    struct U64HashCompare {
        std::size_t hash(const u64& key) const noexcept {
            return std::hash<u64>()(key);
        }
        bool equal(const u64& a, const u64& b) const noexcept {
            return a == b;
        }
    };
    
    // Active executions
    tbb::concurrent_hash_map<u64, ExecutionContext*, U64HashCompare> activeExecutions;
    
    // Node execution records per DAG
    tbb::concurrent_hash_map<DAGId, 
        tbb::concurrent_hash_map<NodeId, NodeExecutionRecord, IdHashCompare<NodeId>>*,
        IdHashCompare<DAGId>> executionRecords;
    
    // Worker threads
    tbb::task_group workers;
    
    // Executor state
    AtomicBool running;
    AtomicU64 nextExecutionId;
    AtomicU64 totalExecutions;
    AtomicU64 failedExecutions;
    
    // Message broker integration
    MessageBroker* broker;
    
    // Configuration
    static constexpr u32 MAX_PARALLEL_NODES = 1024;
    static constexpr u32 MAX_RETRY_COUNT = 3;
    static constexpr u64 EXECUTION_TIMEOUT_CYCLES = 10000000000ULL; // ~3 seconds at 3GHz
    
public:
    // Constructor/Destructor
    DAGExecutor() noexcept;
    ~DAGExecutor() noexcept;
    
    // Initialization
    bool initialize(MessageBroker* msgBroker = nullptr) noexcept;
    void shutdown() noexcept;
    
    // DAG Execution
    u64 executeDag(DAGInstance* dag, const ExecutionContext& context) noexcept;
    bool executeNode(DAGNode* node, ExecutionContext* context) noexcept;
    void cancelExecution(u64 executionId) noexcept;
    
    // Async execution
    u64 executeDagAsync(DAGInstance* dag, const ExecutionContext& context) noexcept;
    bool waitForExecution(u64 executionId, u64 timeoutCycles = 0) noexcept;
    bool getExecutionResult(u64 executionId, ExecutionResult& result) noexcept;
    
    // Node scheduling
    bool scheduleNode(NodeId nodeId, ExecutionContext* context, ExecutionPriority priority) noexcept;
    void processQueues() noexcept;
    bool processQueue(u32 priorityLevel) noexcept;
    
    // Dependency management
    bool updateDependencies(NodeId completedNode, DAGInstance* dag, ExecutionContext* context) noexcept;
    bool checkNodeReady(NodeId nodeId, DAGInstance* dag) noexcept;
    
    // Message routing
    bool routeNodeOutput(DAGNode* node, const Message& output, DAGInstance* dag) noexcept;
    bool sendToDownstream(NodeId sourceNode, NodeId targetNode, const Message& msg, DAGInstance* dag) noexcept;
    
    // Statistics
    u64 getTotalExecutions() const noexcept { return totalExecutions.load(); }
    u64 getFailedExecutions() const noexcept { return failedExecutions.load(); }
    bool getNodeStats(DAGId dagId, NodeId nodeId, NodeExecutionStats& stats) noexcept;
    
    // Worker management
    void startWorkers(u32 numWorkers = 0) noexcept;  // 0 = hardware concurrency
    void stopWorkers() noexcept;
    
private:
    // Internal execution
    void executeNodeInternal(DAGNode* node, NodeExecutionRecord& record, ExecutionContext* context) noexcept;
    void handleNodeFailure(NodeId nodeId, ExecutionContext* context, u32 errorCode) noexcept;
    void finalizeExecution(ExecutionContext* context, DAGInstance* dag) noexcept;
    
    // Worker thread function
    void workerLoop() noexcept;
    
    // Utility
    ExecutionPriority getNodePriority(DAGNode* node) noexcept;
    u64 getRDTSC() noexcept { return __rdtsc(); }
};

// ============================================================================
// GLOBAL DAG EXECUTOR INSTANCE
// ============================================================================
// PSYCHOTIC: Single global executor for entire system
DAGExecutor& getGlobalDAGExecutor() noexcept;

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_DAGEXECUTOR_H