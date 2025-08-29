// Core_Threading.cpp - THREAD MANAGEMENT IMPLEMENTATION
// Thread pool, affinity, and work stealing for extreme concurrency

#include "Core_Threading.h"
#include <cstdio>
#include <chrono>

#if AARENDOCORE_PLATFORM_WINDOWS
    #include <windows.h>
    #include <processthreadsapi.h>
#else
    #include <pthread.h>
    #include <sched.h>
    #include <unistd.h>
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// THREAD LOCAL STORAGE
// ============================================================================

thread_local ThreadContext* t_threadContext = nullptr;

ThreadContext* GetThreadContext() noexcept {
    return t_threadContext;
}

void SetThreadContext(ThreadContext* context) noexcept {
    t_threadContext = context;
}

// ============================================================================
// THREAD POOL IMPLEMENTATION
// ============================================================================

ThreadPool::ThreadPool() noexcept 
    : workers_(nullptr), workerCount_(0), workersPerNode_(0),
      nextWorker_(0), running_(false), nodeQueues_(nullptr), nodeCount_(0) {
}

ThreadPool::ThreadPool(u32 workerCount) noexcept
    : workers_(nullptr), workerCount_(0), workersPerNode_(0),
      nextWorker_(0), running_(false), nodeQueues_(nullptr), nodeCount_(0) {
    initialize(workerCount);
}

ThreadPool::~ThreadPool() noexcept {
    shutdown();
}

bool ThreadPool::initialize(u32 workerCount) noexcept {
    if (running_.load(MemoryOrderAcquire)) {
        return false;  // Already initialized
    }
    
    // Default to hardware thread count
    if (workerCount == 0) {
        workerCount = GetHardwareThreadCount();
        if (workerCount == 0) {
            workerCount = DEFAULT_WORKER_THREADS;
        }
    }
    
    if (workerCount > MAX_WORKER_THREADS) {
        workerCount = MAX_WORKER_THREADS;
    }
    
    workerCount_ = workerCount;
    
    // Get NUMA node count
    nodeCount_ = GetNumaNodeCount();
    if (nodeCount_ == 0) {
        nodeCount_ = 1;
    }
    
    workersPerNode_ = workerCount_ / nodeCount_;
    if (workersPerNode_ == 0) {
        workersPerNode_ = 1;
    }
    
    // Allocate workers
    workers_ = static_cast<Worker*>(
        AllocateAligned(sizeof(Worker) * workerCount_, CACHE_LINE)
    );
    
    if (!workers_) {
        return false;
    }
    
    // Initialize workers
    for (u32 i = 0; i < workerCount_; ++i) {
        new(&workers_[i]) Worker();
    }
    
    // Allocate node queues
    nodeQueues_ = static_cast<NodeQueue*>(
        AllocateAligned(sizeof(NodeQueue) * nodeCount_, CACHE_LINE)
    );
    
    if (!nodeQueues_) {
        FreeAligned(workers_);
        workers_ = nullptr;
        return false;
    }
    
    // Initialize node queues
    for (u32 i = 0; i < nodeCount_; ++i) {
        new(&nodeQueues_[i]) NodeQueue();
        nodeQueues_[i].capacity = TICK_QUEUE_SIZE / nodeCount_;
        nodeQueues_[i].tasks = static_cast<Task*>(
            AllocateAligned(sizeof(Task) * nodeQueues_[i].capacity, CACHE_LINE)
        );
        
        if (!nodeQueues_[i].tasks) {
            // Cleanup on failure
            for (u32 j = 0; j < i; ++j) {
                FreeAligned(nodeQueues_[j].tasks);
            }
            FreeAligned(nodeQueues_);
            FreeAligned(workers_);
            return false;
        }
    }
    
    running_.store(true, MemoryOrderRelease);
    
    // Start worker threads
    for (u32 i = 0; i < workerCount_; ++i) {
        if (!initializeWorker(i)) {
            shutdown();
            return false;
        }
    }
    
    return true;
}

bool ThreadPool::initializeWorker(u32 workerId) noexcept {
    if (workerId >= workerCount_) {
        return false;
    }
    
    Worker& worker = workers_[workerId];
    
    // Initialize context
    worker.context.workerId = workerId;
    worker.context.numaNode = workerId / workersPerNode_;
    worker.context.priority = ThreadPriority::Normal;
    worker.context.state = ThreadState::Created;
    
    std::snprintf(worker.context.name, MAX_THREAD_NAME_LENGTH,
        "Worker_%u_Node_%u", workerId, worker.context.numaNode);
    
    // Start thread
    try {
        worker.thread = std::thread(&ThreadPool::workerFunction, this, workerId);
    } catch (...) {
        return false;
    }
    
    return true;
}

void ThreadPool::workerFunction(u32 workerId) noexcept {
    Worker& worker = workers_[workerId];
    
    // Set thread context
    SetThreadContext(&worker.context);
    
    // Set thread name
    SetThreadName(worker.context.name);
    
    // Set NUMA affinity
    SetThreadNumaAffinity(worker.context.numaNode);
    
    // Update thread ID
    worker.context.threadId = GetCurrentThreadId();
    worker.context.state = ThreadState::Running;
    
    // Worker loop
    while (!worker.shouldStop.test_and_set(MemoryOrderRelaxed)) {
        Task task;
        
        if (worker.hasTask.test_and_set(MemoryOrderAcquire)) {
            // Execute direct task
            worker.taskLock.lock();
            task = std::move(worker.currentTask);
            worker.currentTask = nullptr;
            worker.taskLock.unlock();
            
            if (task) {
                task();
                AtomicIncrement(worker.context.taskCount);
            }
            
            worker.hasTask.clear(MemoryOrderRelease);
        } else if (getTask(workerId, task)) {
            // Execute queued task
            task();
            AtomicIncrement(worker.context.taskCount);
        } else {
            // No work - yield
            YieldThread();
            AtomicIncrement(worker.context.cyclesIdle);
        }
    }
    
    worker.context.state = ThreadState::Terminated;
}

bool ThreadPool::getTask(u32 workerId, Task& task) noexcept {
    // Try local node queue first
    u32 nodeId = workers_[workerId].context.numaNode;
    NodeQueue& queue = nodeQueues_[nodeId];
    
    queue.lock.lock();
    
    u32 head = queue.head.load(MemoryOrderRelaxed);
    u32 tail = queue.tail.load(MemoryOrderRelaxed);
    
    if (head != tail) {
        task = std::move(queue.tasks[head % queue.capacity]);
        queue.head.store(head + 1, MemoryOrderRelaxed);
        queue.lock.unlock();
        return true;
    }
    
    queue.lock.unlock();
    
    // Try stealing from other nodes
    for (u32 i = 1; i < nodeCount_; ++i) {
        u32 targetNode = (nodeId + i) % nodeCount_;
        NodeQueue& targetQueue = nodeQueues_[targetNode];
        
        targetQueue.lock.lock();
        
        head = targetQueue.head.load(MemoryOrderRelaxed);
        tail = targetQueue.tail.load(MemoryOrderRelaxed);
        
        if (head != tail) {
            task = std::move(targetQueue.tasks[head % targetQueue.capacity]);
            targetQueue.head.store(head + 1, MemoryOrderRelaxed);
            targetQueue.lock.unlock();
            
            // Track remote access
            AtomicIncrement(g_numaStats.remoteAccesses[nodeId]);
            
            return true;
        }
        
        targetQueue.lock.unlock();
    }
    
    return false;
}

bool ThreadPool::submit(Task task) noexcept {
    if (!running_.load(MemoryOrderAcquire)) {
        return false;
    }
    
    // Round-robin node selection
    u32 nodeId = nextWorker_.fetch_add(1, MemoryOrderRelaxed) % nodeCount_;
    return submitToNode(std::move(task), nodeId);
}

bool ThreadPool::submitToNode(Task task, u32 nodeId) noexcept {
    if (!running_.load(MemoryOrderAcquire) || nodeId >= nodeCount_) {
        return false;
    }
    
    NodeQueue& queue = nodeQueues_[nodeId];
    
    queue.lock.lock();
    
    u32 head = queue.head.load(MemoryOrderRelaxed);
    u32 tail = queue.tail.load(MemoryOrderRelaxed);
    
    if (tail - head >= queue.capacity) {
        queue.lock.unlock();
        return false;  // Queue full
    }
    
    queue.tasks[tail % queue.capacity] = std::move(task);
    queue.tail.store(tail + 1, MemoryOrderRelaxed);
    
    queue.lock.unlock();
    
    return true;
}

bool ThreadPool::submitToWorker(Task task, u32 workerId) noexcept {
    if (!running_.load(MemoryOrderAcquire) || workerId >= workerCount_) {
        return false;
    }
    
    Worker& worker = workers_[workerId];
    
    // Try direct submission
    if (!worker.hasTask.test_and_set(MemoryOrderAcquire)) {
        worker.taskLock.lock();
        worker.currentTask = std::move(task);
        worker.taskLock.unlock();
        return true;
    }
    
    // Fall back to node queue
    return submitToNode(std::move(task), worker.context.numaNode);
}

void ThreadPool::wait() noexcept {
    while (true) {
        bool allEmpty = true;
        
        // Check all queues
        for (u32 i = 0; i < nodeCount_; ++i) {
            u32 head = nodeQueues_[i].head.load(MemoryOrderRelaxed);
            u32 tail = nodeQueues_[i].tail.load(MemoryOrderRelaxed);
            if (head != tail) {
                allEmpty = false;
                break;
            }
        }
        
        // Check all workers
        if (allEmpty) {
            for (u32 i = 0; i < workerCount_; ++i) {
                if (workers_[i].hasTask.test_and_set(MemoryOrderAcquire)) {
                    workers_[i].hasTask.clear(MemoryOrderRelease);
                    allEmpty = false;
                    break;
                }
            }
        }
        
        if (allEmpty) {
            break;
        }
        
        YieldThread();
    }
}

void ThreadPool::shutdown() noexcept {
    if (!running_.exchange(false, MemoryOrderAcqRel)) {
        return;  // Already shutdown
    }
    
    // Signal workers to stop
    for (u32 i = 0; i < workerCount_; ++i) {
        workers_[i].shouldStop.test_and_set(MemoryOrderRelease);
    }
    
    // Join threads
    for (u32 i = 0; i < workerCount_; ++i) {
        if (workers_[i].thread.joinable()) {
            workers_[i].thread.join();
        }
    }
    
    // Cleanup queues
    if (nodeQueues_) {
        for (u32 i = 0; i < nodeCount_; ++i) {
            if (nodeQueues_[i].tasks) {
                FreeAligned(nodeQueues_[i].tasks);
            }
        }
        FreeAligned(nodeQueues_);
        nodeQueues_ = nullptr;
    }
    
    // Cleanup workers
    if (workers_) {
        for (u32 i = 0; i < workerCount_; ++i) {
            workers_[i].~Worker();
        }
        FreeAligned(workers_);
        workers_ = nullptr;
    }
    
    workerCount_ = 0;
    nodeCount_ = 0;
}

const ThreadContext* ThreadPool::getWorkerContext(u32 workerId) const noexcept {
    if (workerId < workerCount_) {
        return &workers_[workerId].context;
    }
    return nullptr;
}

// ============================================================================
// THREAD UTILITIES IMPLEMENTATION
// ============================================================================

u64 GetCurrentThreadId() noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    return static_cast<u64>(::GetCurrentThreadId());  // Use global scope
#else
    return static_cast<u64>(pthread_self());
#endif
}

bool SetThreadName(const char* name) noexcept {
    if (!name) {
        return false;
    }
    
#if AARENDOCORE_PLATFORM_WINDOWS
    // Windows thread naming
    typedef struct tagTHREADNAME_INFO {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    } THREADNAME_INFO;
    
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = name;
    info.dwThreadID = static_cast<DWORD>(GetCurrentThreadId());
    info.dwFlags = 0;
    
    __try {
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR),
            reinterpret_cast<ULONG_PTR*>(&info));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
    
    return true;
#else
    return pthread_setname_np(pthread_self(), name) == 0;
#endif
}

bool SetThreadPriority(ThreadPriority priority) noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    int winPriority = THREAD_PRIORITY_NORMAL;
    switch (priority) {
        case ThreadPriority::Idle:     winPriority = THREAD_PRIORITY_IDLE; break;
        case ThreadPriority::Low:      winPriority = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::Normal:   winPriority = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::High:     winPriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::Realtime: winPriority = THREAD_PRIORITY_TIME_CRITICAL; break;
    }
    return ::SetThreadPriority(GetCurrentThread(), winPriority) != 0;  // Use global scope
#else
    sched_param param;
    param.sched_priority = static_cast<int>(priority);
    return pthread_setschedparam(pthread_self(), SCHED_OTHER, &param) == 0;
#endif
}

bool SetThreadAffinity(u64 cpuMask) noexcept {
#if AARENDOCORE_PLATFORM_WINDOWS
    return SetThreadAffinityMask(GetCurrentThread(), cpuMask) != 0;
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (u32 i = 0; i < 64; ++i) {
        if (cpuMask & (1ULL << i)) {
            CPU_SET(i, &cpuset);
        }
    }
    
    return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0;
#endif
}

bool SetThreadCpu(u32 cpuId) noexcept {
    return SetThreadAffinity(1ULL << cpuId);
}

u32 GetHardwareThreadCount() noexcept {
    return std::thread::hardware_concurrency();
}

void YieldThread() noexcept {
    std::this_thread::yield();
}

void SleepThread(u64 nanoseconds) noexcept {
    std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
}

// ============================================================================
// THREAD INFORMATION EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetThreadingInfo() {
    static char info[512];
    
    std::snprintf(info, sizeof(info),
        "Threading: HardwareThreads=%u, MaxWorkers=%u, DefaultWorkers=%u",
        GetHardwareThreadCount(),
        MAX_WORKER_THREADS,
        DEFAULT_WORKER_THREADS
    );
    
    return info;
}

extern "C" AARENDOCORE_API u32 AARendoCore_GetHardwareThreads() {
    return GetHardwareThreadCount();
}

AARENDOCORE_NAMESPACE_END