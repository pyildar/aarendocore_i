// Core_NUMA.cpp - NUMA-AWARE MEMORY IMPLEMENTATION
// Platform-specific NUMA operations with extreme precision

#include "Core_NUMA.h"
#include <cstdio>

#if AARENDOCORE_PLATFORM_WINDOWS
    #include <windows.h>
    // Windows NUMA APIs (Vista and later)
#else
    #if __has_include(<numa.h>)
        #include <numa.h>
        #include <numaif.h>
        #define HAS_NUMA_SUPPORT 1
    #else
        #define HAS_NUMA_SUPPORT 0
    #endif
    #include <sched.h>
    #include <pthread.h>
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// GLOBAL NUMA STATISTICS
// ============================================================================

NumaMemoryStats g_numaStats;

// ============================================================================
// NUMA SYSTEM STATE
// ============================================================================

namespace {
    struct NumaSystem {
        bool available;
        u32 nodeCount;
        NumaNodeInfo nodes[MAX_NUMA_NODES];
        
        NumaSystem() : available(false), nodeCount(0) {
            for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
                nodes[i] = {};
            }
        }
    };
    
    NumaSystem g_numaSystem;
}

// ============================================================================
// NUMA MEMORY POOL IMPLEMENTATION
// ============================================================================

NumaMemoryPool::NumaMemoryPool() noexcept 
    : nodeCount_(0), nextNode_(0), initialized_(false) {
    for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
        nodes_[i].nodeId = i;
        nodes_[i].sessionCount.store(0);
    }
}

NumaMemoryPool::~NumaMemoryPool() noexcept {
    release();
}

bool NumaMemoryPool::initialize(u32 nodeCount, usize poolSizePerNode) noexcept {
    if (initialized_) {
        return false;
    }
    
    // Use system node count if not specified
    if (nodeCount == 0) {
        nodeCount = GetNumaNodeCount();
        if (nodeCount == 0) {
            nodeCount = 1;  // Fallback to single node
        }
    }
    
    if (nodeCount > MAX_NUMA_NODES) {
        nodeCount = MAX_NUMA_NODES;
    }
    
    nodeCount_ = nodeCount;
    
    // Initialize per-node pools
    for (u32 i = 0; i < nodeCount_; ++i) {
        if (!nodes_[i].pool.initialize(poolSizePerNode, NUMA_PAGE)) {
            // Cleanup on failure
            for (u32 j = 0; j < i; ++j) {
                nodes_[j].pool.release();
            }
            return false;
        }
        
        nodes_[i].nodeId = i;
        nodes_[i].sessionCount.store(0);
    }
    
    nextNode_.store(0);
    initialized_ = true;
    
    return true;
}

void* NumaMemoryPool::allocateOnNode(u32 nodeId, usize size, u32 alignment) noexcept {
    if (!initialized_ || nodeId >= nodeCount_) {
        return nullptr;
    }
    
    void* ptr = nodes_[nodeId].pool.allocate(size, alignment);
    
    if (ptr) {
        AtomicIncrement(g_numaStats.allocations[nodeId]);
        AtomicAdd(g_numaStats.bytesAllocated[nodeId], size);
    }
    
    return ptr;
}

void* NumaMemoryPool::allocate(usize size, u32 alignment) noexcept {
    if (!initialized_) {
        return nullptr;
    }
    
    // Try least loaded node first
    u32 nodeId = getLeastLoadedNode();
    void* ptr = allocateOnNode(nodeId, size, alignment);
    
    if (!ptr) {
        // Try round-robin if least loaded failed
        for (u32 i = 0; i < nodeCount_; ++i) {
            nodeId = nextNode_.fetch_add(1, MemoryOrderRelaxed) % nodeCount_;
            ptr = allocateOnNode(nodeId, size, alignment);
            if (ptr) {
                break;
            }
        }
    }
    
    return ptr;
}

u32 NumaMemoryPool::getCurrentNode() const noexcept {
    return GetCurrentNumaNode();
}

u32 NumaMemoryPool::getLeastLoadedNode() const noexcept {
    if (!initialized_ || nodeCount_ == 0) {
        return 0;
    }
    
    u32 bestNode = 0;
    u64 minSessions = nodes_[0].sessionCount.load(MemoryOrderRelaxed);
    
    for (u32 i = 1; i < nodeCount_; ++i) {
        u64 sessions = nodes_[i].sessionCount.load(MemoryOrderRelaxed);
        if (sessions < minSessions) {
            minSessions = sessions;
            bestNode = i;
        }
    }
    
    return bestNode;
}

void NumaMemoryPool::reset() noexcept {
    for (u32 i = 0; i < nodeCount_; ++i) {
        nodes_[i].lock.lock();
        nodes_[i].pool.reset();
        nodes_[i].sessionCount.store(0);
        nodes_[i].lock.unlock();
    }
}

void NumaMemoryPool::release() noexcept {
    if (initialized_) {
        for (u32 i = 0; i < nodeCount_; ++i) {
            nodes_[i].pool.release();
            nodes_[i].sessionCount.store(0);
        }
        
        nodeCount_ = 0;
        nextNode_.store(0);
        initialized_ = false;
    }
}

NumaNodeInfo NumaMemoryPool::getNodeInfo(u32 nodeId) const noexcept {
    if (nodeId < MAX_NUMA_NODES) {
        return g_numaSystem.nodes[nodeId];
    }
    return {};
}

// ============================================================================
// NUMA SYSTEM FUNCTIONS
// ============================================================================

bool InitializeNUMA() noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    // Windows NUMA initialization
    ULONG highestNode = 0;
    if (!GetNumaHighestNodeNumber(&highestNode)) {
        g_numaSystem.available = false;
        g_numaSystem.nodeCount = 1;
        return false;
    }
    
    g_numaSystem.nodeCount = highestNode + 1;
    if (g_numaSystem.nodeCount > MAX_NUMA_NODES) {
        g_numaSystem.nodeCount = MAX_NUMA_NODES;
    }
    
    // Get node information
    for (u32 i = 0; i < g_numaSystem.nodeCount; ++i) {
        ULONGLONG mask = 0;
        GetNumaNodeProcessorMask(static_cast<UCHAR>(i), &mask);
        
        g_numaSystem.nodes[i].nodeId = i;
        g_numaSystem.nodes[i].cpuMask = mask;
        g_numaSystem.nodes[i].cpuCount = static_cast<u32>(__popcnt64(mask));
        g_numaSystem.nodes[i].available = true;
        
        // Memory info would require additional APIs
        g_numaSystem.nodes[i].totalMemory = 0;
        g_numaSystem.nodes[i].freeMemory = 0;
    }
    
    g_numaSystem.available = true;
    return true;
    
#else
    // Linux NUMA initialization
#if HAS_NUMA_SUPPORT
    if (numa_available() < 0) {
        g_numaSystem.available = false;
        g_numaSystem.nodeCount = 1;
        return false;
    }
    
    g_numaSystem.nodeCount = numa_num_configured_nodes();
    if (g_numaSystem.nodeCount > MAX_NUMA_NODES) {
        g_numaSystem.nodeCount = MAX_NUMA_NODES;
    }
    
    // Get node information
    for (u32 i = 0; i < g_numaSystem.nodeCount; ++i) {
        g_numaSystem.nodes[i].nodeId = i;
        g_numaSystem.nodes[i].totalMemory = numa_node_size64(i, nullptr);
        g_numaSystem.nodes[i].available = true;
        
        // Get CPU count for node
        struct bitmask* cpus = numa_allocate_cpumask();
        if (numa_node_to_cpus(i, cpus) == 0) {
            g_numaSystem.nodes[i].cpuCount = numa_bitmask_weight(cpus);
        }
        numa_free_cpumask(cpus);
    }
    
    g_numaSystem.available = true;
    return true;
#else
    // No NUMA support on this Linux system
    g_numaSystem.available = false;
    g_numaSystem.nodeCount = 1;
    return false;
#endif  // HAS_NUMA_SUPPORT
#endif  // AARENDOCORE_PLATFORM_WINDOWS
}

void ShutdownNUMA() noexcept {
    // Reset NUMA system state
    g_numaSystem.available = false;
    g_numaSystem.nodeCount = 0;
    
    for (u32 i = 0; i < MAX_NUMA_NODES; ++i) {
        g_numaSystem.nodes[i] = {};
    }
}

u32 GetNumaNodeCount() noexcept {
    if (!g_numaSystem.available) {
        InitializeNUMA();
    }
    return g_numaSystem.nodeCount;
}

u32 GetCurrentNumaNode() noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    PROCESSOR_NUMBER procNumber;
    GetCurrentProcessorNumberEx(&procNumber);
    
    USHORT nodeNumber;
    if (GetNumaProcessorNodeEx(&procNumber, &nodeNumber)) {
        return static_cast<u32>(nodeNumber);
    }
    return 0;
#else
    #if HAS_NUMA_SUPPORT
        return numa_node_of_cpu(sched_getcpu());
    #else
        return 0;
    #endif
#endif
}

bool SetThreadNumaAffinity(u32 nodeId) noexcept {
    if (nodeId >= g_numaSystem.nodeCount) {
        return false;
    }
    
#if AARENDOCORE_PLATFORM_WINDOWS
    GROUP_AFFINITY affinity = {};
    if (GetNumaNodeProcessorMaskEx(static_cast<USHORT>(nodeId), &affinity)) {
        return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != 0;
    }
    return false;
#else
    #if HAS_NUMA_SUPPORT
    struct bitmask* mask = numa_allocate_cpumask();
    numa_node_to_cpus(nodeId, mask);
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int i = 0; i < numa_num_configured_cpus(); ++i) {
        if (numa_bitmask_isbitset(mask, i)) {
            CPU_SET(i, &cpuset);
        }
    }
    
    numa_free_cpumask(mask);
    
    return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0;
    #else
    return false;  // No NUMA support
    #endif
#endif
}

void* AllocateOnNumaNode(u32 nodeId, usize size, usize alignment) noexcept {
    void* ptr = AllocateAligned(size, alignment);
    
    if (ptr && nodeId < g_numaSystem.nodeCount) {
#if AARENDOCORE_PLATFORM_WINDOWS
        // Windows doesn't have direct NUMA allocation in user mode
        // Would need to use VirtualAllocExNuma with current process handle
#else
        #if HAS_NUMA_SUPPORT
        // Bind memory to NUMA node
        numa_tonode_memory(ptr, size, nodeId);
        #endif
#endif
        
        AtomicIncrement(g_numaStats.allocations[nodeId]);
        AtomicAdd(g_numaStats.bytesAllocated[nodeId], size);
    }
    
    return ptr;
}

void FreeNumaMemory(void* ptr) noexcept {
    FreeAligned(ptr);
}

bool IsNumaAvailable() noexcept {
    if (!g_numaSystem.available) {
        InitializeNUMA();
    }
    return g_numaSystem.available;
}

u32 GetNumaNodeForAddress(const void* addr) noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    // Windows doesn't provide easy way to query NUMA node for address
    (void)addr;  // Suppress unused parameter warning
    return 0;
#else
    #if HAS_NUMA_SUPPORT
    int node = -1;
    if (get_mempolicy(&node, nullptr, 0, const_cast<void*>(addr), MPOL_F_NODE | MPOL_F_ADDR) == 0) {
        return static_cast<u32>(node);
    }
    #endif
    return 0;
#endif
}

bool MigratePagesToNode(void* addr, usize size, u32 nodeId) noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    // Windows doesn't support page migration in user mode
    (void)addr; (void)size; (void)nodeId;
    return false;
#else
    #if HAS_NUMA_SUPPORT
    if (nodeId >= g_numaSystem.nodeCount) {
        return false;
    }
    
    unsigned long node_mask = 1UL << nodeId;
    return mbind(addr, size, MPOL_BIND, &node_mask, sizeof(node_mask) * 8, MPOL_MF_MOVE) == 0;
    #else
    (void)addr; (void)size; (void)nodeId;
    return false;
    #endif
#endif
}

bool PrefaultPages(void* addr, usize size) noexcept {
    // Touch each page to prefault it
    volatile byte* ptr = static_cast<volatile byte*>(addr);
    for (usize i = 0; i < size; i += PAGE_SIZE) {
        ptr[i] = 0;
    }
    return true;
}

// ============================================================================
// NUMA INFORMATION EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetNumaInfo() {
    static char info[512];
    
    if (!g_numaSystem.available) {
        InitializeNUMA();
    }
    
    std::snprintf(info, sizeof(info),
        "NUMA: Available=%s, Nodes=%u, Node0_CPUs=%u, Node0_Mask=0x%llX",
        g_numaSystem.available ? "Yes" : "No",
        g_numaSystem.nodeCount,
        g_numaSystem.nodes[0].cpuCount,
        static_cast<unsigned long long>(g_numaSystem.nodes[0].cpuMask)
    );
    
    return info;
}

extern "C" AARENDOCORE_API u32 AARendoCore_GetNumaNodes() {
    return GetNumaNodeCount();
}

extern "C" AARENDOCORE_API u32 AARendoCore_GetCurrentNode() {
    return GetCurrentNumaNode();
}

AARENDOCORE_NAMESPACE_END