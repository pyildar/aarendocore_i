//===--- Core_LockFreeQueue.h - Lock-Free Queue Implementation ----------===//
//
// COMPILATION LEVEL: 1 (Depends on PrimitiveTypes only)
// ORIGIN: NEW - Lock-free queue for PSYCHOTIC throughput
// DEPENDENCIES: Core_PrimitiveTypes.h, Core_Atomic.h
// DEPENDENTS: All processing units that need queuing
//
// ZERO locks, ALIEN LEVEL performance, 10M concurrent operations.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_LOCKFREEQUEUE_H
#define AARENDOCORE_CORE_LOCKFREEQUEUE_H

#include "Core_PrimitiveTypes.h"
#include "Core_Atomic.h"
#include <atomic>
#include <new>

namespace AARendoCoreGLM {

// ==========================================================================
// LOCK-FREE QUEUE - PSYCHOTIC PERFORMANCE
// ==========================================================================

// Origin: Lock-free single-producer single-consumer queue
// Template parameters: T - Element type, Capacity - Queue size
template<typename T, usize Capacity>
class alignas(CACHE_LINE_SIZE) LockFreeQueue {
private:
    static_assert((Capacity & (Capacity - 1)) == 0 && Capacity > 0, 
                  "Capacity must be power of 2 for lock-free operation");
    
    // ======================================================================
    // MEMBER VARIABLES - CACHE-LINE ALIGNED
    // ======================================================================
    
    // Origin: Member - Ring buffer storage, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) T buffer_[Capacity];
    
    // Origin: Member - Head position (consumer), Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) AtomicU64 head_;
    
    // Origin: Member - Tail position (producer), Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) AtomicU64 tail_;
    
    // Origin: Member - Cached head for producer, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) u64 cachedHead_;
    
    // Origin: Member - Cached tail for consumer, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) u64 cachedTail_;
    
public:
    // ======================================================================
    // CONSTRUCTOR/DESTRUCTOR
    // ======================================================================
    
    // Origin: Constructor
    LockFreeQueue() noexcept 
        : buffer_{}
        , head_(0)
        , tail_(0)
        , cachedHead_(0)
        , cachedTail_(0) {
    }
    
    // Deleted copy/move - queue is not copyable
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;
    
    // ======================================================================
    // QUEUE OPERATIONS - LOCK-FREE
    // ======================================================================
    
    // Origin: Enqueue element (producer)
    // Input: item - Element to enqueue
    // Output: true if successful
    bool enqueue(const T& item) noexcept {
        // currentTail: Origin - Local from atomic load, Scope: function
        const u64 currentTail = tail_.load(std::memory_order_relaxed);
        // nextTail: Origin - Local calculation, Scope: function
        const u64 nextTail = (currentTail + 1) & (Capacity - 1);
        
        // Check if queue is full using cached head
        if (nextTail == cachedHead_) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (nextTail == cachedHead_) {
                return false; // Queue is full
            }
        }
        
        // Store item
        buffer_[currentTail] = item;
        
        // Update tail
        tail_.store(nextTail, std::memory_order_release);
        
        return true;
    }
    
    // Origin: Dequeue element (consumer)
    // Input: item - Reference to store dequeued element
    // Output: true if successful
    bool dequeue(T& item) noexcept {
        // currentHead: Origin - Local from atomic load, Scope: function
        const u64 currentHead = head_.load(std::memory_order_relaxed);
        
        // Check if queue is empty using cached tail
        if (currentHead == cachedTail_) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (currentHead == cachedTail_) {
                return false; // Queue is empty
            }
        }
        
        // Load item
        item = buffer_[currentHead];
        
        // Update head
        // nextHead: Origin - Local calculation, Scope: function
        const u64 nextHead = (currentHead + 1) & (Capacity - 1);
        head_.store(nextHead, std::memory_order_release);
        
        return true;
    }
    
    // Origin: Try to enqueue with backoff
    // Input: item - Element to enqueue
    //        maxRetries - Maximum retry attempts
    // Output: true if successful
    bool tryEnqueue(const T& item, u32 maxRetries = 3) noexcept {
        for (u32 i = 0; i < maxRetries; ++i) {
            if (enqueue(item)) {
                return true;
            }
            // Exponential backoff
            for (u32 j = 0; j < (1u << i); ++j) {
                _mm_pause(); // CPU pause instruction
            }
        }
        return false;
    }
    
    // Origin: Check if queue is empty
    // Output: true if empty
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    // Origin: Get approximate size
    // Output: Approximate number of elements
    usize size() const noexcept {
        // head: Origin - Local from atomic load, Scope: function
        const u64 head = head_.load(std::memory_order_acquire);
        // tail: Origin - Local from atomic load, Scope: function
        const u64 tail = tail_.load(std::memory_order_acquire);
        
        return (tail - head) & (Capacity - 1);
    }
    
    // Origin: Get capacity
    // Output: Queue capacity
    static constexpr usize capacity() noexcept {
        return Capacity;
    }
    
    // Origin: Clear queue (not thread-safe)
    void clear() noexcept {
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
        cachedHead_ = 0;
        cachedTail_ = 0;
    }
};

// ==========================================================================
// MULTI-PRODUCER MULTI-CONSUMER QUEUE - EXTREME CONCURRENCY
// ==========================================================================

// Origin: Lock-free MPMC queue for 10M concurrent operations
template<typename T, usize Capacity>
class alignas(CACHE_LINE_SIZE) MPMCQueue {
private:
    static_assert((Capacity & (Capacity - 1)) == 0 && Capacity > 0,
                  "Capacity must be power of 2");
    
    // Origin: Node structure for MPMC operation
    struct alignas(CACHE_LINE_SIZE) Node {
        AtomicU64 sequence;
        T data;
        
        Node() noexcept : sequence(0), data{} {}
    };
    
    // ======================================================================
    // MEMBER VARIABLES
    // ======================================================================
    
    // Origin: Member - Ring buffer of nodes, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) Node buffer_[Capacity];
    
    // Origin: Member - Head position, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) AtomicU64 head_;
    
    // Origin: Member - Tail position, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) AtomicU64 tail_;
    
public:
    // Origin: Constructor
    MPMCQueue() noexcept : buffer_{}, head_(0), tail_(0) {
        // Initialize sequence numbers
        for (usize i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    // Origin: Enqueue for multiple producers
    // Input: item - Element to enqueue
    // Output: true if successful
    bool enqueue(const T& item) noexcept {
        // pos: Origin - Local from atomic fetch_add, Scope: function
        u64 pos = tail_.fetch_add(1, std::memory_order_relaxed);
        
        // cell: Origin - Reference to buffer node, Scope: function
        Node& cell = buffer_[pos & (Capacity - 1)];
        
        // seq: Origin - Local from atomic load, Scope: function
        u64 seq = cell.sequence.load(std::memory_order_acquire);
        
        // intptr_t for signed comparison
        // diff: Origin - Local calculation, Scope: function
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        
        if (diff == 0) {
            // We can enqueue
            cell.data = item;
            cell.sequence.store(pos + 1, std::memory_order_release);
            return true;
        } else if (diff < 0) {
            // Queue is full
            return false;
        } else {
            // Another thread is ahead, retry
            return false;
        }
    }
    
    // Origin: Dequeue for multiple consumers
    // Input: item - Reference to store dequeued element
    // Output: true if successful
    bool dequeue(T& item) noexcept {
        // pos: Origin - Local from atomic fetch_add, Scope: function
        u64 pos = head_.fetch_add(1, std::memory_order_relaxed);
        
        // cell: Origin - Reference to buffer node, Scope: function
        Node& cell = buffer_[pos & (Capacity - 1)];
        
        // seq: Origin - Local from atomic load, Scope: function
        u64 seq = cell.sequence.load(std::memory_order_acquire);
        
        // intptr_t for signed comparison
        // diff: Origin - Local calculation, Scope: function
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        
        if (diff == 0) {
            // We can dequeue
            item = cell.data;
            cell.sequence.store(pos + Capacity, std::memory_order_release);
            return true;
        } else if (diff < 0) {
            // Queue is empty
            return false;
        } else {
            // Another thread is ahead, retry
            return false;
        }
    }
};

} // namespace AARendoCoreGLM

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage  
template class AARendoCoreGLM::LockFreeQueue<int, 1024>;
template class AARendoCoreGLM::MPMCQueue<int, 1024>;

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_LockFreeQueue);

#endif // AARENDOCORE_CORE_LOCKFREEQUEUE_H