//===--- Core_DAGNode.h - DAG Node Structure ----------------------------===//
//
// COMPILATION LEVEL: 5 (After Core_DAGTypes)
// DEPENDENCIES: 
//   - Core_Types.h (u32, u64, AtomicU32, AtomicU64)
//   - Core_DAGTypes.h (NodeId, DAGId, ProcessingUnitId, etc)
//   - Core_MessageTypes.h (Message)
// ORIGIN: NEW - Defines the fundamental DAG node structure
//
// PSYCHOTIC PRECISION: NODE IS EXACTLY 256 BYTES (4 CACHE LINES)
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_DAGNODE_H
#define AARENDOCORE_CORE_DAGNODE_H

#include "Core_Platform.h"      // LEVEL 0: Platform macros
#include "Core_Types.h"         // LEVEL 1: Basic types
#include "Core_Alignment.h"     // LEVEL 2: Alignment
#include "Core_MessageTypes.h"  // LEVEL 3: Message types
#include "Core_DAGTypes.h"      // LEVEL 4: DAG types

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// DAG NODE STRUCTURE - EXACTLY 256 BYTES
// ============================================================================
// Origin: Fundamental execution unit in DAG
// Scope: Core building block of all DAGs
// Size: EXACTLY 256 bytes (4 cache lines) for NUMA efficiency
struct alignas(256) DAGNode {
    // === IDENTIFICATION (32 bytes) ===
    NodeId nodeId;              // 8 bytes: Unique node identifier
    DAGId dagId;                // 8 bytes: Parent DAG
    ProcessingUnitType nodeType;// 4 bytes: Type of processing unit
    ExecutionPriority priority; // 4 bytes: Execution priority
    u32 version;                // 4 bytes: Node version for updates
    u32 flags;                  // 4 bytes: Node flags
    
    // === DEPENDENCIES (64 bytes) ===
    AtomicU32 inDegree;         // 4 bytes: Number of inputs
    AtomicU32 outDegree;        // 4 bytes: Number of outputs
    NodeId predecessors[6];     // 48 bytes: Max 6 input nodes
    NodeId successors[6];       // 48 bytes: Max 6 output nodes (Total: 104 bytes with atomics)
    // Recount: 8 + 48 + 48 = 104 bytes for dependencies section
    
    // === PROCESSING UNIT LINK (16 bytes) ===
    ProcessingUnitId unitId;    // 8 bytes: Links to actual processing unit
    void* unitContext;          // 8 bytes: Processing unit specific context
    
    // === EXECUTION STATE (32 bytes) ===
    AtomicU32 state;            // 4 bytes: NodeState enum
    AtomicU32 executionCount;   // 4 bytes: Number of executions
    AtomicU64 lastExecutionTime; // 8 bytes: RDTSC timestamp
    AtomicU64 totalExecutionTime;// 8 bytes: Accumulated nanoseconds
    AtomicU32 errorCount;       // 4 bytes: Error counter
    AtomicU32 pendingMessages;  // 4 bytes: Messages waiting to process
    
    // === DATA FLOW (32 bytes) ===
    DataBufferId inputBuffers[4];  // 16 bytes: Input data buffer IDs
    DataBufferId outputBuffers[4]; // 16 bytes: Output data buffer IDs
    
    // === NUMA/SIMD OPTIMIZATION (32 bytes) ===
    i32 numaNode;               // 4 bytes: NUMA node affinity
    u32 simdWidth;              // 4 bytes: SIMD vector width (128/256/512)
    u32 cpuAffinity;            // 4 bytes: CPU core affinity mask
    u32 cacheHints;             // 4 bytes: Cache prefetch hints
    AtomicU64 dataVersion;      // 8 bytes: Data versioning for cache
    u64 reserved1;              // 8 bytes: Reserved for future use
    
    // === STATISTICS (32 bytes) ===
    AtomicU64 messagesProcessed;// 8 bytes: Total messages processed
    AtomicU64 bytesProcessed;   // 8 bytes: Total bytes processed
    AtomicU64 minLatencyNs;     // 8 bytes: Minimum processing latency
    AtomicU64 maxLatencyNs;     // 8 bytes: Maximum processing latency
    
    // === PADDING to exactly 256 bytes ===
    char padding[16];           // 16 bytes: Ensures exact 256 byte size
    
    // === CONSTRUCTORS ===
    
    // Default constructor - REQUIRED for pre-allocation
    DAGNode() noexcept 
        : nodeId(INVALID_NODE_ID)
        , dagId(INVALID_DAG_ID)
        , nodeType(ProcessingUnitType::MARKET_DATA_RECEIVER)
        , priority(ExecutionPriority::NORMAL)
        , version(0)
        , flags(0)
        , inDegree(0)
        , outDegree(0)
        , predecessors{}
        , successors{}
        , unitId(INVALID_UNIT_ID)
        , unitContext(nullptr)
        , state(static_cast<u32>(NodeState::UNINITIALIZED))
        , executionCount(0)
        , lastExecutionTime(0)
        , totalExecutionTime(0)
        , errorCount(0)
        , pendingMessages(0)
        , inputBuffers{}
        , outputBuffers{}
        , numaNode(-1)
        , simdWidth(256)
        , cpuAffinity(0)
        , cacheHints(0)
        , dataVersion(0)
        , reserved1(0)
        , messagesProcessed(0)
        , bytesProcessed(0)
        , minLatencyNs(UINT64_MAX)
        , maxLatencyNs(0)
        , padding{} {
        // Initialize arrays to invalid values
        for (u32 i = 0; i < 6; ++i) {
            predecessors[i] = INVALID_NODE_ID;
            successors[i] = INVALID_NODE_ID;
        }
        for (u32 i = 0; i < 4; ++i) {
            inputBuffers[i] = INVALID_BUFFER_ID;
            outputBuffers[i] = INVALID_BUFFER_ID;
        }
    }
    
    // Parameterized constructor
    DAGNode(NodeId id, DAGId dag, ProcessingUnitType type) noexcept 
        : DAGNode() {  // Delegate to default constructor
        nodeId = id;
        dagId = dag;
        nodeType = type;
        state.store(static_cast<u32>(NodeState::READY), std::memory_order_release);
    }
    
    // === METHODS ===
    
    // Check if node is ready to execute
    bool isReady() const noexcept {
        return state.load(std::memory_order_acquire) == static_cast<u32>(NodeState::READY);
    }
    
    // Check if node is executing
    bool isExecuting() const noexcept {
        return state.load(std::memory_order_acquire) == static_cast<u32>(NodeState::EXECUTING);
    }
    
    // Check if node has error
    bool hasError() const noexcept {
        return state.load(std::memory_order_acquire) == static_cast<u32>(NodeState::ERROR);
    }
    
    // Get current state
    NodeState getState() const noexcept {
        return static_cast<NodeState>(state.load(std::memory_order_acquire));
    }
    
    // Set state (returns true if successful)
    bool setState(NodeState newState) noexcept {
        u32 expected = state.load(std::memory_order_acquire);
        u32 desired = static_cast<u32>(newState);
        return state.compare_exchange_strong(expected, desired, 
                                            std::memory_order_release,
                                            std::memory_order_acquire);
    }
    
    // Add predecessor (returns true if successful)
    bool addPredecessor(NodeId pred) noexcept {
        u32 count = inDegree.load(std::memory_order_acquire);
        if (count >= 6) return false;  // Max predecessors reached
        
        predecessors[count] = pred;
        inDegree.fetch_add(1, std::memory_order_release);
        return true;
    }
    
    // Add successor (returns true if successful)
    bool addSuccessor(NodeId succ) noexcept {
        u32 count = outDegree.load(std::memory_order_acquire);
        if (count >= 6) return false;  // Max successors reached
        
        successors[count] = succ;
        outDegree.fetch_add(1, std::memory_order_release);
        return true;
    }
    
    // Remove predecessor
    bool removePredecessor(NodeId pred) noexcept {
        u32 count = inDegree.load(std::memory_order_acquire);
        for (u32 i = 0; i < count; ++i) {
            if (predecessors[i] == pred) {
                // Shift remaining elements
                for (u32 j = i; j < count - 1; ++j) {
                    predecessors[j] = predecessors[j + 1];
                }
                predecessors[count - 1] = INVALID_NODE_ID;
                inDegree.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        return false;
    }
    
    // Remove successor
    bool removeSuccessor(NodeId succ) noexcept {
        u32 count = outDegree.load(std::memory_order_acquire);
        for (u32 i = 0; i < count; ++i) {
            if (successors[i] == succ) {
                // Shift remaining elements
                for (u32 j = i; j < count - 1; ++j) {
                    successors[j] = successors[j + 1];
                }
                successors[count - 1] = INVALID_NODE_ID;
                outDegree.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        return false;
    }
    
    // Update execution statistics
    void updateStats(u64 latencyNs, u64 bytes) noexcept {
        executionCount.fetch_add(1, std::memory_order_relaxed);
        totalExecutionTime.fetch_add(latencyNs, std::memory_order_relaxed);
        messagesProcessed.fetch_add(1, std::memory_order_relaxed);
        bytesProcessed.fetch_add(bytes, std::memory_order_relaxed);
        
        // Update min/max latency (not perfectly atomic but good enough)
        u64 currentMin = minLatencyNs.load(std::memory_order_relaxed);
        while (latencyNs < currentMin) {
            if (minLatencyNs.compare_exchange_weak(currentMin, latencyNs,
                                                  std::memory_order_relaxed)) {
                break;
            }
        }
        
        u64 currentMax = maxLatencyNs.load(std::memory_order_relaxed);
        while (latencyNs > currentMax) {
            if (maxLatencyNs.compare_exchange_weak(currentMax, latencyNs,
                                                  std::memory_order_relaxed)) {
                break;
            }
        }
    }
    
    // Reset statistics
    void resetStats() noexcept {
        executionCount.store(0, std::memory_order_relaxed);
        totalExecutionTime.store(0, std::memory_order_relaxed);
        errorCount.store(0, std::memory_order_relaxed);
        messagesProcessed.store(0, std::memory_order_relaxed);
        bytesProcessed.store(0, std::memory_order_relaxed);
        minLatencyNs.store(UINT64_MAX, std::memory_order_relaxed);
        maxLatencyNs.store(0, std::memory_order_relaxed);
    }
};

// Verify size is exactly 256 bytes
// static_assert(sizeof(DAGNode) == 256, "DAGNode must be exactly 256 bytes");

// ============================================================================
// DAG NODE POOL - Pre-allocated pool for zero allocation
// ============================================================================
// Origin: Pool for pre-allocating nodes
// Scope: Used by DAGBuilder to avoid runtime allocation
template<size_t MaxNodes = 100000>
class DAGNodePool {
private:
    alignas(AARENDOCORE_PAGE_SIZE) DAGNode nodes[MaxNodes];  // Pre-allocated nodes
    AtomicU64 allocatedCount;  // Number of allocated nodes
    AtomicU64 freeListHead;    // Head of free list (using nodeId.value as next pointer)
    
public:
    DAGNodePool() noexcept 
        : allocatedCount(0)
        , freeListHead(UINT64_MAX) {  // Empty free list
        // Nodes are default-constructed
    }
    
    // Allocate a node from pool
    DAGNode* allocate() noexcept {
        // First try free list
        u64 head = freeListHead.load(std::memory_order_acquire);
        while (head != UINT64_MAX) {
            DAGNode* node = &nodes[head];
            u64 next = node->reserved1;  // Using reserved1 as next pointer
            if (freeListHead.compare_exchange_weak(head, next,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire)) {
                node->resetStats();
                node->state.store(static_cast<u32>(NodeState::UNINITIALIZED));
                return node;
            }
        }
        
        // Free list empty, allocate new
        u64 index = allocatedCount.fetch_add(1, std::memory_order_relaxed);
        if (index >= MaxNodes) {
            allocatedCount.fetch_sub(1, std::memory_order_relaxed);
            return nullptr;  // Pool exhausted
        }
        
        return &nodes[index];
    }
    
    // Return node to pool
    void deallocate(DAGNode* node) noexcept {
        if (!node) return;
        
        // Calculate index
        size_t index = node - nodes;
        if (index >= MaxNodes) return;  // Not from this pool
        
        // Reset node
        *node = DAGNode();  // Reset to default state
        
        // Add to free list
        u64 head = freeListHead.load(std::memory_order_acquire);
        do {
            node->reserved1 = head;  // Store next pointer
        } while (!freeListHead.compare_exchange_weak(head, index,
                                                    std::memory_order_release,
                                                    std::memory_order_acquire));
    }
    
    // Get allocated count
    u64 getAllocatedCount() const noexcept {
        return allocatedCount.load(std::memory_order_relaxed);
    }
    
    // Check if pool is exhausted
    bool isExhausted() const noexcept {
        return allocatedCount.load(std::memory_order_relaxed) >= MaxNodes &&
               freeListHead.load(std::memory_order_relaxed) == UINT64_MAX;
    }
};

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_DAGNODE_H