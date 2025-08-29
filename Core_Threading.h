// Core_Threading.h - THREAD PRIMITIVES AND MANAGEMENT
// COMPILER PROCESSES EIGHTH - Final piece of Phase 1
// Thread management for 10M concurrent sessions
// Every thread pinned, every context switch minimized

#ifndef AARENDOCOREGLM_CORE_THREADING_H
#define AARENDOCOREGLM_CORE_THREADING_H

#include "Core_Platform.h"   // Foundation
#include "Core_Types.h"      // Type system  
#include "Core_Config.h"     // System constants
#include "Core_Alignment.h"  // Alignment utilities
#include "Core_Atomic.h"     // Atomic operations
#include "Core_Memory.h"     // Memory management
#include "Core_NUMA.h"       // NUMA awareness

#include <thread>            // std::thread
#include <functional>        // std::function

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// THREAD CONSTANTS - For extreme thread management
// ============================================================================

constexpr u32 MAX_THREAD_NAME_LENGTH = 32;
constexpr u32 DEFAULT_STACK_SIZE = 2 * MB;
constexpr u32 WORKER_STACK_SIZE = 8 * MB;

// Thread priorities
enum class ThreadPriority : i32 {
    Idle = -2,
    Low = -1,
    Normal = 0,
    High = 1,
    Realtime = 2
};

// Thread state
enum class ThreadState : u32 {
    Created = 0,
    Running = 1,
    Suspended = 2,
    Waiting = 3,
    Terminated = 4
};

// ============================================================================
// THREAD CONTEXT - Per-thread information
// ============================================================================

struct CACHE_ALIGNED ThreadContext {
    u64 threadId;                            // System thread ID
    u32 workerId;                            // Worker ID (0-based)
    u32 numaNode;                            // NUMA node affinity
    u64 cpuMask;                             // CPU affinity mask
    ThreadPriority priority;                 // Thread priority
    ThreadState state;                       // Current state
    char name[MAX_THREAD_NAME_LENGTH];       // Thread name
    
    // Performance counters
    AtomicU64 taskCount{0};                  // Tasks processed
    AtomicU64 cyclesActive{0};               // Active CPU cycles
    AtomicU64 cyclesIdle{0};                 // Idle CPU cycles
    AtomicU64 contextSwitches{0};            // Context switches
    
    // Padding to prevent false sharing
    byte padding[CACHE_LINE - (sizeof(u64) * 4) % CACHE_LINE];
};

// ============================================================================
// THREAD POOL - Manages worker threads with NUMA awareness
// ============================================================================

class ThreadPool {
public:
    using Task = std::function<void()>;
    
private:
    struct CACHE_ALIGNED Worker {
        std::thread thread;                  // Worker thread
        ThreadContext context;                // Thread context
        AtomicFlag shouldStop;                // Stop flag
        AtomicFlag hasTask;                   // Task available flag
        Task currentTask;                     // Current task
        Spinlock taskLock;                    // Task lock
    };
    
    Worker* workers_;                        // Worker array
    u32 workerCount_;                        // Number of workers
    u32 workersPerNode_;                     // Workers per NUMA node
    AtomicU32 nextWorker_;                   // Round-robin counter
    AtomicBool running_;                     // Pool running state
    
    // Task queue per NUMA node for locality
    struct NodeQueue {
        Task* tasks;                         // Task array
        AtomicU32 head{0};                   // Queue head
        AtomicU32 tail{0};                   // Queue tail
        u32 capacity;                        // Queue capacity
        Spinlock lock;                        // Queue lock
    };
    
    NodeQueue* nodeQueues_;                  // Per-node queues
    u32 nodeCount_;                          // Number of NUMA nodes
    
public:
    ThreadPool() noexcept;
    explicit ThreadPool(u32 workerCount) noexcept;
    ~ThreadPool() noexcept;
    
    // Disable copy
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    // Initialize pool
    bool initialize(u32 workerCount = 0) noexcept;
    
    // Submit task to pool
    bool submit(Task task) noexcept;
    
    // Submit task to specific NUMA node
    bool submitToNode(Task task, u32 nodeId) noexcept;
    
    // Submit task to specific worker
    bool submitToWorker(Task task, u32 workerId) noexcept;
    
    // Wait for all tasks to complete
    void wait() noexcept;
    
    // Shutdown pool
    void shutdown() noexcept;
    
    // Get worker count
    u32 getWorkerCount() const noexcept { return workerCount_; }
    
    // Get worker context
    const ThreadContext* getWorkerContext(u32 workerId) const noexcept;
    
private:
    // Worker thread function
    void workerFunction(u32 workerId) noexcept;
    
    // Get task from queues
    bool getTask(u32 workerId, Task& task) noexcept;
    
    // Initialize worker
    bool initializeWorker(u32 workerId) noexcept;
};

// ============================================================================
// THREAD UTILITIES - Thread management functions
// ============================================================================

// Get current thread ID
u64 GetCurrentThreadId() noexcept;

// Set thread name
bool SetThreadName(const char* name) noexcept;

// Set thread priority
bool SetThreadPriority(ThreadPriority priority) noexcept;

// Set thread affinity to CPU mask
bool SetThreadAffinity(u64 cpuMask) noexcept;

// Set thread affinity to single CPU
bool SetThreadCpu(u32 cpuId) noexcept;

// Get number of hardware threads
u32 GetHardwareThreadCount() noexcept;

// Yield current thread
void YieldThread() noexcept;

// Sleep thread for nanoseconds
void SleepThread(u64 nanoseconds) noexcept;

// ============================================================================
// THREAD LOCAL STORAGE - Per-thread data
// ============================================================================

// Thread-local context pointer
extern thread_local ThreadContext* t_threadContext;

// Get current thread context
ThreadContext* GetThreadContext() noexcept;

// Set current thread context
void SetThreadContext(ThreadContext* context) noexcept;

// ============================================================================
// WORK STEALING QUEUE - For load balancing
// ============================================================================

template<typename T, usize CAPACITY = 1024>
class WorkStealingQueue {
private:
    struct Entry {
        std::atomic<T*> data{nullptr};
    };
    
    alignas(CACHE_LINE) Entry buffer_[CAPACITY];
    alignas(CACHE_LINE) AtomicU64 top_{0};
    alignas(CACHE_LINE) AtomicU64 bottom_{0};
    
public:
    WorkStealingQueue() noexcept = default;
    
    // Push item (owner thread only)
    bool push(T* item) noexcept {
        u64 b = bottom_.load(MemoryOrderRelaxed);
        u64 t = top_.load(MemoryOrderAcquire);
        
        if (b - t >= CAPACITY) {
            return false;  // Queue full
        }
        
        buffer_[b % CAPACITY].data.store(item, MemoryOrderRelaxed);
        std::atomic_thread_fence(MemoryOrderRelease);
        bottom_.store(b + 1, MemoryOrderRelaxed);
        
        return true;
    }
    
    // Pop item (owner thread only)
    T* pop() noexcept {
        u64 b = bottom_.load(MemoryOrderRelaxed) - 1;
        bottom_.store(b, MemoryOrderRelaxed);
        
        std::atomic_thread_fence(MemoryOrderSeqCst);
        
        u64 t = top_.load(MemoryOrderRelaxed);
        
        if (t > b) {
            bottom_.store(b + 1, MemoryOrderRelaxed);
            return nullptr;  // Queue empty
        }
        
        T* item = buffer_[b % CAPACITY].data.load(MemoryOrderRelaxed);
        
        if (t == b) {
            // Last item - compete with stealers
            if (!top_.compare_exchange_strong(t, t + 1,
                MemoryOrderSeqCst, MemoryOrderRelaxed)) {
                item = nullptr;  // Lost race
            }
            bottom_.store(b + 1, MemoryOrderRelaxed);
        }
        
        return item;
    }
    
    // Steal item (other threads)
    T* steal() noexcept {
        u64 t = top_.load(MemoryOrderAcquire);
        
        std::atomic_thread_fence(MemoryOrderSeqCst);
        
        u64 b = bottom_.load(MemoryOrderAcquire);
        
        if (t >= b) {
            return nullptr;  // Queue empty
        }
        
        T* item = buffer_[t % CAPACITY].data.load(MemoryOrderRelaxed);
        
        if (!top_.compare_exchange_strong(t, t + 1,
            MemoryOrderSeqCst, MemoryOrderRelaxed)) {
            return nullptr;  // Lost race
        }
        
        return item;
    }
    
    // Check if empty
    bool empty() const noexcept {
        u64 b = bottom_.load(MemoryOrderRelaxed);
        u64 t = top_.load(MemoryOrderRelaxed);
        return b <= t;
    }
    
    // Get size estimate
    usize size() const noexcept {
        u64 b = bottom_.load(MemoryOrderRelaxed);
        u64 t = top_.load(MemoryOrderRelaxed);
        return (b > t) ? (b - t) : 0;
    }
};

// ============================================================================
// STATIC ASSERTIONS
// ============================================================================

// Verify ThreadContext is cache aligned
static_assert(sizeof(ThreadContext) % CACHE_LINE == 0,
    "ThreadContext must be cache line sized");

// Verify work stealing queue capacity is power of 2
static_assert((1024 & (1024 - 1)) == 0,
    "Work stealing queue capacity must be power of 2");

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCOREGLM_CORE_THREADING_H