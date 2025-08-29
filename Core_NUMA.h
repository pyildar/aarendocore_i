// Core_NUMA.h - NUMA-AWARE MEMORY ALLOCATION
// COMPILER PROCESSES SEVENTH - After base memory management
// Non-Uniform Memory Access optimization for 10M sessions
// Each session bound to specific NUMA node for cache locality

#ifndef AARENDOCOREGLM_CORE_NUMA_H
#define AARENDOCOREGLM_CORE_NUMA_H

#include "Core_Platform.h"   // Foundation
#include "Core_Types.h"      // Type system
#include "Core_Config.h"     // System constants
#include "Core_Alignment.h"  // Alignment utilities
#include "Core_Atomic.h"     // Atomic operations
#include "Core_Memory.h"     // Base memory management

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// NUMA CONSTANTS - For our 10M session distribution
// ============================================================================

constexpr u32 MAX_NUMA_NODES = 8;                    // Maximum NUMA nodes supported
constexpr u32 DEFAULT_NUMA_NODES = 4;                // Default for 10M sessions (2.5M each)
constexpr usize NUMA_LOCAL_POOL_SIZE = 256 * MB;     // Per-node memory pool
constexpr usize NUMA_HUGE_PAGE_SIZE = 2 * MB;        // 2MB huge pages

// ============================================================================
// NUMA NODE INFORMATION
// ============================================================================

struct NumaNodeInfo {
    u32 nodeId;                              // NUMA node ID
    u64 totalMemory;                         // Total memory on node
    u64 freeMemory;                          // Free memory on node
    u32 cpuCount;                            // Number of CPUs on node
    u64 cpuMask;                             // CPU affinity mask
    bool available;                          // Node is available
};

// ============================================================================
// NUMA MEMORY STATISTICS - Per-node tracking
// ============================================================================

struct NumaMemoryStats {
    AtomicU64 allocations[MAX_NUMA_NODES];   // Allocations per node
    AtomicU64 bytesAllocated[MAX_NUMA_NODES];// Bytes allocated per node
    AtomicU64 localAccesses[MAX_NUMA_NODES]; // Local memory accesses
    AtomicU64 remoteAccesses[MAX_NUMA_NODES];// Remote memory accesses
};

// Global NUMA statistics
extern NumaMemoryStats g_numaStats;

// ============================================================================
// NUMA MEMORY POOL - Per-node memory pools
// ============================================================================

class NumaMemoryPool {
private:
    struct NodePool {
        MemoryPool pool;                     // Memory pool for this node
        u32 nodeId;                           // NUMA node ID
        AtomicU64 sessionCount;               // Sessions on this node
        Spinlock lock;                        // Per-node lock
    };
    
    NodePool nodes_[MAX_NUMA_NODES];         // Per-node pools
    u32 nodeCount_;                          // Number of NUMA nodes
    AtomicU32 nextNode_;                     // Round-robin node selection
    bool initialized_;                       // Initialization status
    
public:
    NumaMemoryPool() noexcept;
    ~NumaMemoryPool() noexcept;
    
    // Disable copy
    NumaMemoryPool(const NumaMemoryPool&) = delete;
    NumaMemoryPool& operator=(const NumaMemoryPool&) = delete;
    
    // Initialize NUMA pools
    bool initialize(u32 nodeCount = 0, usize poolSizePerNode = NUMA_LOCAL_POOL_SIZE) noexcept;
    
    // Allocate on specific NUMA node
    void* allocateOnNode(u32 nodeId, usize size, u32 alignment = CACHE_LINE) noexcept;
    
    // Allocate with automatic node selection
    void* allocate(usize size, u32 alignment = CACHE_LINE) noexcept;
    
    // Get optimal NUMA node for current thread
    u32 getCurrentNode() const noexcept;
    
    // Get least loaded node
    u32 getLeastLoadedNode() const noexcept;
    
    // Reset all node pools
    void reset() noexcept;
    
    // Release all node pools
    void release() noexcept;
    
    // Get node information
    NumaNodeInfo getNodeInfo(u32 nodeId) const noexcept;
    u32 getNodeCount() const noexcept { return nodeCount_; }
    bool isInitialized() const noexcept { return initialized_; }
};

// ============================================================================
// NUMA-AWARE ALLOCATION FUNCTIONS
// ============================================================================

// Initialize NUMA system
bool InitializeNUMA() noexcept;

// Shutdown NUMA system
void ShutdownNUMA() noexcept;

// Get number of NUMA nodes
u32 GetNumaNodeCount() noexcept;

// Get current thread's NUMA node
u32 GetCurrentNumaNode() noexcept;

// Set thread affinity to NUMA node
bool SetThreadNumaAffinity(u32 nodeId) noexcept;

// Allocate memory on specific NUMA node
void* AllocateOnNumaNode(u32 nodeId, usize size, usize alignment = CACHE_LINE) noexcept;

// Free NUMA allocated memory
void FreeNumaMemory(void* ptr) noexcept;

// ============================================================================
// NUMA-AWARE DATA STRUCTURE - Distributes data across nodes
// ============================================================================

template<typename T>
class NumaDistributed {
private:
    struct alignas(NUMA_PAGE) NodeData {
        T data[SESSIONS_PER_NUMA_NODE / MAX_NUMA_NODES];
        AtomicU32 count{0};
    };
    
    NodeData* nodes_[MAX_NUMA_NODES];
    u32 nodeCount_;
    
public:
    NumaDistributed() noexcept : nodeCount_(0) {
        for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
            nodes_[i] = nullptr;
        }
    }
    
    ~NumaDistributed() noexcept {
        release();
    }
    
    // Initialize for specified number of nodes
    bool initialize(u32 nodeCount) noexcept {
        if (nodeCount == 0 || nodeCount > MAX_NUMA_NODES) {
            return false;
        }
        
        nodeCount_ = nodeCount;
        
        for (u32 i = 0; i < nodeCount_; ++i) {
            nodes_[i] = static_cast<NodeData*>(
                AllocateOnNumaNode(i, sizeof(NodeData), NUMA_PAGE)
            );
            
            if (!nodes_[i]) {
                // Cleanup on failure
                for (u32 j = 0; j < i; ++j) {
                    FreeNumaMemory(nodes_[j]);
                    nodes_[j] = nullptr;
                }
                return false;
            }
            
            // Initialize the data
            new(nodes_[i]) NodeData();
        }
        
        return true;
    }
    
    // Add item to specific node
    bool addToNode(u32 nodeId, const T& item) noexcept {
        if (nodeId >= nodeCount_ || !nodes_[nodeId]) {
            return false;
        }
        
        u32 index = nodes_[nodeId]->count.fetch_add(1, MemoryOrderRelaxed);
        if (index >= (SESSIONS_PER_NUMA_NODE / MAX_NUMA_NODES)) {
            nodes_[nodeId]->count.fetch_sub(1, MemoryOrderRelaxed);
            return false;
        }
        
        nodes_[nodeId]->data[index] = item;
        return true;
    }
    
    // Get item from specific node
    T* getFromNode(u32 nodeId, u32 index) noexcept {
        if (nodeId >= nodeCount_ || !nodes_[nodeId]) {
            return nullptr;
        }
        
        if (index >= nodes_[nodeId]->count.load(MemoryOrderRelaxed)) {
            return nullptr;
        }
        
        return &nodes_[nodeId]->data[index];
    }
    
    // Release all node data
    void release() noexcept {
        for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
            if (nodes_[i]) {
                nodes_[i]->~NodeData();
                FreeNumaMemory(nodes_[i]);
                nodes_[i] = nullptr;
            }
        }
        nodeCount_ = 0;
    }
};

// ============================================================================
// NUMA UTILITY FUNCTIONS
// ============================================================================

// Check if system supports NUMA
bool IsNumaAvailable() noexcept;

// Get memory policy for address
u32 GetNumaNodeForAddress(const void* addr) noexcept;

// Migrate pages to NUMA node
bool MigratePagesToNode(void* addr, usize size, u32 nodeId) noexcept;

// Prefault pages for NUMA locality
bool PrefaultPages(void* addr, usize size) noexcept;

// ============================================================================
// STATIC ASSERTIONS
// ============================================================================

// Verify NUMA page alignment
static_assert(NUMA_PAGE == NUMA_HUGE_PAGE_SIZE, 
    "NUMA page should match huge page size");

// Verify session distribution
static_assert(SESSIONS_PER_NUMA_NODE * DEFAULT_NUMA_NODES == MAX_CONCURRENT_SESSIONS,
    "Session distribution across NUMA nodes must equal total sessions");

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCOREGLM_CORE_NUMA_H