//===--- Core_MessageBroker.h - Zero-Copy Message Routing ---------------===//
//
// COMPILATION LEVEL: 8 (After DAGBuilder, StreamMultiplexer)
// DEPENDENCIES: 
//   - Core_Types.h (u32, u64, AtomicU32, AtomicU64)
//   - Core_MessageTypes.h (Message, MessageType)
//   - Core_DAGTypes.h (NodeId, DAGId)
//   - Core_StreamMultiplexer.h (for integration)
// ORIGIN: NEW - Zero-copy pub/sub message broker
//
// PSYCHOTIC PRECISION: LOCK-FREE, ZERO-ALLOCATION MESSAGE ROUTING
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_MESSAGEBROKER_H
#define AARENDOCORE_CORE_MESSAGEBROKER_H

#include "Core_Platform.h"
#include "Core_PrimitiveTypes.h"
#include "Core_Types.h"
#include "Core_MessageTypes.h"
#include "Core_DAGTypes.h"
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_vector.h>
#include <atomic>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// TOPIC ID CONSTANTS - Using TopicId from Core_PrimitiveTypes.h
// ============================================================================

// Invalid topic ID constant  
constexpr TopicId INVALID_TOPIC_ID(0u);

// ============================================================================
// SUBSCRIPTION ID - Unique subscription identifier
// ============================================================================
struct SubscriptionId {
    u64 value;
    
    constexpr SubscriptionId() noexcept : value(0) {}
    constexpr explicit SubscriptionId(u64 v) noexcept : value(v) {}
    
    constexpr bool operator==(const SubscriptionId& other) const noexcept {
        return value == other.value;
    }
};

// Invalid subscription ID
constexpr SubscriptionId INVALID_SUBSCRIPTION_ID(0);

// ============================================================================
// MESSAGE PRIORITY - For priority routing
// ============================================================================
enum class MessagePriority : u32 {
    CRITICAL = 0,   // Highest priority
    HIGH = 1,
    NORMAL = 2,
    LOW = 3,
    BULK = 4        // Lowest priority
};

// ============================================================================
// DELIVERY MODE - Message delivery guarantees
// ============================================================================
enum class DeliveryMode : u32 {
    AT_MOST_ONCE = 0,   // Fire and forget
    AT_LEAST_ONCE = 1,  // Retry until ACK
    EXACTLY_ONCE = 2    // Deduplication + retry
};

// ============================================================================
// MESSAGE ENVELOPE - Message with routing metadata
// ============================================================================
struct alignas(128) MessageEnvelope {
    Message message;           // 64 bytes - The actual message
    TopicId topic;            // 8 bytes - Target topic
    u64 expiryTime;           // 8 bytes - RDTSC expiry timestamp
    u32 retryCount;           // 4 bytes - Retry attempts
    MessagePriority priority;  // 4 bytes - Message priority
    DeliveryMode deliveryMode; // 4 bytes - Delivery guarantee
    u32 hopCount;             // 4 bytes - Routing hops
    NodeId routingPath[4];    // 32 bytes - Up to 4 routing hops
    // Total: 128 bytes (2 cache lines)
    
    MessageEnvelope() noexcept 
        : message()
        , topic(INVALID_TOPIC_ID)
        , expiryTime(0)
        , retryCount(0)
        , priority(MessagePriority::NORMAL)
        , deliveryMode(DeliveryMode::AT_MOST_ONCE)
        , hopCount(0)
        , routingPath{} {}
};

// ============================================================================
// TOPIC STATISTICS - Per-topic metrics
// ============================================================================
struct alignas(64) TopicStats {
    AtomicU64 messagesPublished;
    AtomicU64 messagesDelivered;
    AtomicU64 messagesDropped;
    AtomicU64 messagesExpired;
    AtomicU64 bytesTransferred;
    AtomicU64 lastPublishTime;
    AtomicU64 lastDeliveryTime;
    u64 reserved;  // Padding to 64 bytes
    
    TopicStats() noexcept 
        : messagesPublished(0)
        , messagesDelivered(0)
        , messagesDropped(0)
        , messagesExpired(0)
        , bytesTransferred(0)
        , lastPublishTime(0)
        , lastDeliveryTime(0)
        , reserved(0) {}
};

// ============================================================================
// MESSAGE HANDLER - Callback for message delivery
// ============================================================================
// PSYCHOTIC: No std::function! Use function pointer + context for speed
struct MessageHandler {
    typedef void (*HandlerFunc)(const Message& msg, void* context);
    
    HandlerFunc handler;
    void* context;
    NodeId targetNode;
    u32 filterMask;  // Message type filter
    
    MessageHandler() noexcept 
        : handler(nullptr)
        , context(nullptr)
        , targetNode(INVALID_NODE_ID)
        , filterMask(0xFFFFFFFF) {}  // Accept all by default
        
    MessageHandler(HandlerFunc h, void* ctx) noexcept 
        : handler(h)
        , context(ctx)
        , targetNode(INVALID_NODE_ID)
        , filterMask(0xFFFFFFFF) {}
};

// ============================================================================
// MESSAGE RING BUFFER - Lock-free circular buffer for topics
// ============================================================================
template<size_t Size>
class MessageRingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
private:
    alignas(AARENDOCORE_CACHE_LINE_SIZE) Message buffer[Size];
    alignas(AARENDOCORE_CACHE_LINE_SIZE) AtomicU64 writePos;
    alignas(AARENDOCORE_CACHE_LINE_SIZE) AtomicU64 readPos;
    alignas(AARENDOCORE_CACHE_LINE_SIZE) AtomicU64 committedPos;
    
public:
    static constexpr size_t BUFFER_SIZE = Size;
    static constexpr size_t BUFFER_MASK = Size - 1;
    
    MessageRingBuffer() noexcept 
        : buffer{}
        , writePos(0)
        , readPos(0)
        , committedPos(0) {}
    
    // Try to reserve space for writing
    bool reserve(u64& slot) noexcept {
        u64 pos = writePos.fetch_add(1, std::memory_order_relaxed);
        u64 readPosLocal = readPos.load(std::memory_order_acquire);
        
        if (pos - readPosLocal >= Size) {
            return false;  // Buffer full
        }
        
        slot = pos;
        return true;
    }
    
    // Write message to reserved slot
    void write(u64 slot, const Message& msg) noexcept {
        buffer[slot & BUFFER_MASK] = msg;
        
        // Wait for our turn to commit
        u64 expected = slot;
        while (!committedPos.compare_exchange_weak(expected, slot + 1,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
            expected = slot;
            _mm_pause();  // CPU pause for spin-wait
        }
    }
    
    // Try to read a message
    bool read(Message& msg) noexcept {
        u64 pos = readPos.load(std::memory_order_relaxed);
        u64 committed = committedPos.load(std::memory_order_acquire);
        
        if (pos >= committed) {
            return false;  // No messages available
        }
        
        msg = buffer[pos & BUFFER_MASK];
        readPos.fetch_add(1, std::memory_order_release);
        return true;
    }
    
    // Check if empty
    bool empty() const noexcept {
        return readPos.load(std::memory_order_relaxed) >= 
               committedPos.load(std::memory_order_relaxed);
    }
    
    // Get approximate count
    size_t count() const noexcept {
        u64 w = committedPos.load(std::memory_order_relaxed);
        u64 r = readPos.load(std::memory_order_relaxed);
        return (w >= r) ? (w - r) : 0;
    }
};

// ============================================================================
// TOPIC INFO - Runtime information for a topic
// ============================================================================
struct TopicInfo {
    char name[64];                              // Topic name
    MessageRingBuffer<65536>* buffer;           // 64K messages per topic
    tbb::concurrent_vector<SubscriptionId> subscribers;
    TopicStats stats;
    AtomicU32 active;
    MessagePriority minPriority;                // Minimum priority to accept
    
    TopicInfo() noexcept 
        : name{}
        , buffer(nullptr)
        , subscribers()
        , stats()
        , active(1)
        , minPriority(MessagePriority::BULK) {}
};

// ============================================================================
// SUBSCRIBER INFO - Information about a subscription
// ============================================================================
struct SubscriberInfo {
    TopicId topic;
    MessageHandler handler;
    AtomicU64 messagesReceived;
    AtomicU64 lastDeliveryTime;
    bool active;
    
    SubscriberInfo() noexcept 
        : topic(INVALID_TOPIC_ID)
        , handler()
        , messagesReceived(0)
        , lastDeliveryTime(0)
        , active(false) {}
};

// ============================================================================
// MESSAGE BROKER - Main broker class
// ============================================================================
class MessageBroker {
private:
    // Topic registry - PSYCHOTIC: Using custom hash for TopicId!
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>> topics;
    
    // Subscriber registry
    tbb::concurrent_hash_map<SubscriptionId, SubscriberInfo, IdHashCompare<SubscriptionId>> subscribers;
    
    // Dead letter queue for failed messages
    tbb::concurrent_queue<MessageEnvelope> deadLetterQueue;
    
    // ID generators
    AtomicU32 nextTopicId;
    AtomicU64 nextSubscriptionId;
    
    // Global statistics
    struct BrokerStats {
        AtomicU64 totalMessagesRouted;
        AtomicU64 totalMessagesDropped;
        AtomicU64 totalBytesTransferred;
        AtomicU64 deadLetterCount;
    } stats;
    
    // Configuration
    static constexpr u32 MAX_TOPICS = 1024;
    static constexpr u32 MAX_SUBSCRIPTIONS = 65536;
    static constexpr u32 MAX_DEAD_LETTERS = 10000;
    
public:
    // Constructor/Destructor
    MessageBroker() noexcept;
    ~MessageBroker() noexcept;
    
    // Topic management
    TopicId createTopic(const char* name, MessagePriority minPriority = MessagePriority::BULK) noexcept;
    bool deleteTopic(TopicId topic) noexcept;
    bool topicExists(TopicId topic) const noexcept;
    
    // Publishing
    bool publish(TopicId topic, const Message& msg, MessagePriority priority = MessagePriority::NORMAL) noexcept;
    bool publishBatch(TopicId topic, const Message* messages, u32 count, MessagePriority priority = MessagePriority::NORMAL) noexcept;
    bool publishEnvelope(const MessageEnvelope& envelope) noexcept;
    
    // Subscribing
    SubscriptionId subscribe(TopicId topic, MessageHandler handler) noexcept;
    bool unsubscribe(SubscriptionId subscription) noexcept;
    bool setMessageFilter(SubscriptionId subscription, u32 messageTypeMask) noexcept;
    
    // Message processing
    void processMessages() noexcept;
    void processTopic(TopicId topic) noexcept;
    bool routeMessage(const MessageEnvelope& envelope) noexcept;
    
    // Dead letter queue
    bool sendToDeadLetter(const MessageEnvelope& envelope, u32 reason) noexcept;
    bool retrieveDeadLetter(MessageEnvelope& envelope) noexcept;
    u32 getDeadLetterCount() const noexcept;
    
    // Statistics
    bool getTopicStats(TopicId topic, TopicStats& outStats) const noexcept;
    u64 getTotalMessagesRouted() const noexcept { return stats.totalMessagesRouted.load(); }
    u64 getTotalMessagesDropped() const noexcept { return stats.totalMessagesDropped.load(); }
    u64 getTotalBytesTransferred() const noexcept { return stats.totalBytesTransferred.load(); }
    
    // Utility
    TopicId getTopicByName(const char* name) const noexcept;
    void clearDeadLetters() noexcept;
    void reset() noexcept;
    
private:
    // Internal helpers
    bool deliverToSubscriber(const Message& msg, const SubscriberInfo& subscriber) noexcept;
    bool isMessageExpired(const MessageEnvelope& envelope) const noexcept;
    void updateTopicStats(TopicInfo* topic, bool delivered, u64 bytes) noexcept;
};

// ============================================================================
// GLOBAL MESSAGE BROKER INSTANCE
// ============================================================================
// PSYCHOTIC: Single global broker for entire system
MessageBroker& getGlobalMessageBroker() noexcept;

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_MESSAGEBROKER_H