//===--- Core_MessageBroker.cpp - Zero-Copy Message Routing Implementation ===//
//
// COMPILATION LEVEL: 8 (After DAGBuilder, StreamMultiplexer)
// ORIGIN: Implementation of Core_MessageBroker.h
//
// PSYCHOTIC PRECISION: LOCK-FREE, ZERO-ALLOCATION MESSAGE ROUTING
//===----------------------------------------------------------------------===//

#include "Core_MessageBroker.h"
#include <cstring>
#include <immintrin.h>  // For _mm_pause()

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// GLOBAL MESSAGE BROKER INSTANCE
// ============================================================================
static MessageBroker g_globalBroker;

MessageBroker& getGlobalMessageBroker() noexcept {
    return g_globalBroker;
}

// ============================================================================
// MESSAGE BROKER IMPLEMENTATION
// ============================================================================

// Constructor
MessageBroker::MessageBroker() noexcept 
    : topics()
    , subscribers()
    , deadLetterQueue()
    , nextTopicId(1)  // Start from 1, 0 is invalid
    , nextSubscriptionId(1)
    , stats{0, 0, 0, 0} {
}

// Destructor
MessageBroker::~MessageBroker() noexcept {
    // Clean up all topic buffers
    topics.clear();  // TopicInfo pointers will be cleaned up
    
    // Note: In production, we'd properly deallocate MessageRingBuffer instances
    // But with pre-allocated pools, they're managed separately
}

// Create a new topic
TopicId MessageBroker::createTopic(const char* name, MessagePriority minPriority) noexcept {
    if (!name || topics.size() >= MAX_TOPICS) {
        return INVALID_TOPIC_ID;
    }
    
    // Generate new topic ID
    u64 id = nextTopicId.fetch_add(1, std::memory_order_relaxed);
    TopicId topicId(id);
    
    // Create topic info
    TopicInfo* info = new TopicInfo();  // PSYCHOTIC: In production, use pre-allocated pool!
    std::strncpy(info->name, name, 63);
    info->name[63] = '\0';
    info->minPriority = minPriority;
    
    // Allocate ring buffer - PSYCHOTIC: Should be from pre-allocated pool!
    info->buffer = new MessageRingBuffer<65536>();
    
    // Insert into registry
    topics.insert(std::make_pair(topicId, info));
    
    return topicId;
}

// Delete a topic
bool MessageBroker::deleteTopic(TopicId topic) noexcept {
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::accessor accessor;
    if (!topics.find(accessor, topic)) {
        return false;
    }
    
    TopicInfo* info = accessor->second;
    info->active.store(0, std::memory_order_release);
    
    // Mark all subscribers as inactive
    for (const auto& subId : info->subscribers) {
        tbb::concurrent_hash_map<SubscriptionId, SubscriberInfo, IdHashCompare<SubscriptionId>>::accessor subAccessor;
        if (subscribers.find(subAccessor, subId)) {
            subAccessor->second.active = false;
        }
    }
    
    // Remove from registry
    topics.erase(accessor);
    
    // Clean up buffers (in production, return to pool)
    delete info->buffer;
    delete info;
    
    return true;
}

// Check if topic exists
bool MessageBroker::topicExists(TopicId topic) const noexcept {
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::const_accessor accessor;
    return topics.find(accessor, topic);
}

// Publish a message to a topic
bool MessageBroker::publish(TopicId topic, const Message& msg, MessagePriority priority) noexcept {
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::accessor accessor;
    if (!topics.find(accessor, topic)) {
        stats.totalMessagesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    TopicInfo* info = accessor->second;
    
    // Check priority filter
    if (priority > info->minPriority) {
        stats.totalMessagesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Check if topic is active
    if (info->active.load(std::memory_order_acquire) == 0) {
        stats.totalMessagesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Reserve slot in ring buffer
    u64 slot;
    if (!info->buffer->reserve(slot)) {
        // Buffer full - send to dead letter queue
        MessageEnvelope envelope;
        envelope.message = msg;
        envelope.topic = topic;
        envelope.priority = priority;
        envelope.expiryTime = createTimestamp() + 1000000000;  // 1 second expiry
        sendToDeadLetter(envelope, 1);  // Reason: buffer full
        
        info->stats.messagesDropped.fetch_add(1, std::memory_order_relaxed);
        stats.totalMessagesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Write message to buffer
    info->buffer->write(slot, msg);
    
    // Update statistics
    info->stats.messagesPublished.fetch_add(1, std::memory_order_relaxed);
    info->stats.bytesTransferred.fetch_add(sizeof(Message), std::memory_order_relaxed);
    info->stats.lastPublishTime.store(createTimestamp(), std::memory_order_relaxed);
    
    stats.totalMessagesRouted.fetch_add(1, std::memory_order_relaxed);
    stats.totalBytesTransferred.fetch_add(sizeof(Message), std::memory_order_relaxed);
    
    return true;
}

// Publish batch of messages
bool MessageBroker::publishBatch(TopicId topic, const Message* messages, u32 count, MessagePriority priority) noexcept {
    if (!messages || count == 0) return false;
    
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::accessor accessor;
    if (!topics.find(accessor, topic)) {
        stats.totalMessagesDropped.fetch_add(count, std::memory_order_relaxed);
        return false;
    }
    
    TopicInfo* info = accessor->second;
    
    // Check priority and active status
    if (priority > info->minPriority || info->active.load(std::memory_order_acquire) == 0) {
        stats.totalMessagesDropped.fetch_add(count, std::memory_order_relaxed);
        return false;
    }
    
    u32 published = 0;
    for (u32 i = 0; i < count; ++i) {
        u64 slot;
        if (!info->buffer->reserve(slot)) {
            break;  // Buffer full
        }
        
        info->buffer->write(slot, messages[i]);
        published++;
    }
    
    // Update statistics
    if (published > 0) {
        info->stats.messagesPublished.fetch_add(published, std::memory_order_relaxed);
        info->stats.bytesTransferred.fetch_add(published * sizeof(Message), std::memory_order_relaxed);
        info->stats.lastPublishTime.store(createTimestamp(), std::memory_order_relaxed);
        
        stats.totalMessagesRouted.fetch_add(published, std::memory_order_relaxed);
        stats.totalBytesTransferred.fetch_add(published * sizeof(Message), std::memory_order_relaxed);
    }
    
    if (published < count) {
        u32 dropped = count - published;
        info->stats.messagesDropped.fetch_add(dropped, std::memory_order_relaxed);
        stats.totalMessagesDropped.fetch_add(dropped, std::memory_order_relaxed);
    }
    
    return published == count;
}

// Publish message envelope
bool MessageBroker::publishEnvelope(const MessageEnvelope& envelope) noexcept {
    // Check expiry
    if (isMessageExpired(envelope)) {
        stats.totalMessagesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    return publish(envelope.topic, envelope.message, envelope.priority);
}

// Subscribe to a topic
SubscriptionId MessageBroker::subscribe(TopicId topic, MessageHandler handler) noexcept {
    // Verify topic exists
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::accessor topicAccessor;
    if (!topics.find(topicAccessor, topic)) {
        return INVALID_SUBSCRIPTION_ID;
    }
    
    // Generate subscription ID
    u64 id = nextSubscriptionId.fetch_add(1, std::memory_order_relaxed);
    SubscriptionId subId(id);
    
    // Create subscriber info
    SubscriberInfo info;
    info.topic = topic;
    info.handler = handler;
    info.active = true;
    
    // Add to subscriber registry
    {
        tbb::concurrent_hash_map<SubscriptionId, SubscriberInfo, IdHashCompare<SubscriptionId>>::accessor subAccessor;
        subscribers.insert(subAccessor, subId);
        // PSYCHOTIC: Manual copy because SubscriberInfo has atomic members
        subAccessor->second.topic = info.topic;
        subAccessor->second.handler = info.handler;
        subAccessor->second.messagesReceived.store(info.messagesReceived.load(std::memory_order_relaxed), std::memory_order_relaxed);
        subAccessor->second.lastDeliveryTime.store(info.lastDeliveryTime.load(std::memory_order_relaxed), std::memory_order_relaxed);
        subAccessor->second.active = info.active;
    }
    
    // Add to topic's subscriber list
    topicAccessor->second->subscribers.push_back(subId);
    
    return subId;
}

// Unsubscribe from a topic
bool MessageBroker::unsubscribe(SubscriptionId subscription) noexcept {
    tbb::concurrent_hash_map<SubscriptionId, SubscriberInfo, IdHashCompare<SubscriptionId>>::accessor accessor;
    if (!subscribers.find(accessor, subscription)) {
        return false;
    }
    
    accessor->second.active = false;
    
    // Remove from topic's subscriber list
    TopicId topic = accessor->second.topic;
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::accessor topicAccessor;
    if (topics.find(topicAccessor, topic)) {
        auto& subs = topicAccessor->second->subscribers;
        // PSYCHOTIC: In production, use more efficient removal
        for (auto it = subs.begin(); it != subs.end(); ++it) {
            if (*it == subscription) {
                // Note: concurrent_vector doesn't have erase, need different approach
                // For now, just mark as invalid
                *it = INVALID_SUBSCRIPTION_ID;
                break;
            }
        }
    }
    
    // Remove from registry
    subscribers.erase(accessor);
    return true;
}

// Set message filter for subscription
bool MessageBroker::setMessageFilter(SubscriptionId subscription, u32 messageTypeMask) noexcept {
    tbb::concurrent_hash_map<SubscriptionId, SubscriberInfo, IdHashCompare<SubscriptionId>>::accessor accessor;
    if (!subscribers.find(accessor, subscription)) {
        return false;
    }
    
    accessor->second.handler.filterMask = messageTypeMask;
    return true;
}

// Process all messages in all topics
void MessageBroker::processMessages() noexcept {
    // PSYCHOTIC: Process each topic
    for (auto it = topics.begin(); it != topics.end(); ++it) {
        processTopic(it->first);
    }
    
    // Process dead letters if needed
    MessageEnvelope envelope;
    u32 deadLettersProcessed = 0;
    while (deadLetterQueue.try_pop(envelope) && deadLettersProcessed < 100) {
        // Try to re-route dead letters
        if (envelope.retryCount < 3) {
            envelope.retryCount++;
            if (!publishEnvelope(envelope)) {
                // Failed again, put back in dead letter queue
                deadLetterQueue.push(envelope);
            }
        }
        deadLettersProcessed++;
    }
}

// Process messages for a specific topic
void MessageBroker::processTopic(TopicId topic) noexcept {
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::accessor accessor;
    if (!topics.find(accessor, topic)) {
        return;
    }
    
    TopicInfo* info = accessor->second;
    if (info->active.load(std::memory_order_acquire) == 0) {
        return;
    }
    
    // Process up to 1000 messages per call
    Message msg;
    u32 processed = 0;
    
    while (info->buffer->read(msg) && processed < 1000) {
        // Deliver to all subscribers
        for (const auto& subId : info->subscribers) {
            if (subId == INVALID_SUBSCRIPTION_ID) continue;
            
            tbb::concurrent_hash_map<SubscriptionId, SubscriberInfo, IdHashCompare<SubscriptionId>>::accessor subAccessor;
            if (subscribers.find(subAccessor, subId)) {
                if (subAccessor->second.active) {
                    deliverToSubscriber(msg, subAccessor->second);
                }
            }
        }
        
        info->stats.messagesDelivered.fetch_add(1, std::memory_order_relaxed);
        info->stats.lastDeliveryTime.store(createTimestamp(), std::memory_order_relaxed);
        processed++;
    }
}

// Route a message based on envelope
bool MessageBroker::routeMessage(const MessageEnvelope& envelope) noexcept {
    // Check for routing loops
    if (envelope.hopCount >= 4) {
        sendToDeadLetter(envelope, 2);  // Reason: too many hops
        return false;
    }
    
    // Check expiry
    if (isMessageExpired(envelope)) {
        sendToDeadLetter(envelope, 3);  // Reason: expired
        return false;
    }
    
    return publishEnvelope(envelope);
}

// Send message to dead letter queue
bool MessageBroker::sendToDeadLetter(const MessageEnvelope& envelope, u32 reason) noexcept {
    UNREFERENCED_PARAMETER(reason);  // PSYCHOTIC: Will be used for dead letter categorization
    
    if (stats.deadLetterCount.load(std::memory_order_relaxed) >= MAX_DEAD_LETTERS) {
        return false;  // Dead letter queue full
    }
    
    deadLetterQueue.push(envelope);
    stats.deadLetterCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Retrieve message from dead letter queue
bool MessageBroker::retrieveDeadLetter(MessageEnvelope& envelope) noexcept {
    if (deadLetterQueue.try_pop(envelope)) {
        stats.deadLetterCount.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

// Get dead letter count
u32 MessageBroker::getDeadLetterCount() const noexcept {
    return static_cast<u32>(stats.deadLetterCount.load(std::memory_order_relaxed));
}

// Get topic statistics
bool MessageBroker::getTopicStats(TopicId topic, TopicStats& outStats) const noexcept {
    tbb::concurrent_hash_map<TopicId, TopicInfo*, IdHashCompare<TopicId>>::const_accessor accessor;
    if (!topics.find(accessor, topic)) {
        return false;
    }
    
    // PSYCHOTIC: Copy atomic values individually since TopicStats has atomics
    const TopicStats& srcStats = accessor->second->stats;
    outStats.messagesPublished.store(srcStats.messagesPublished.load(std::memory_order_relaxed), std::memory_order_relaxed);
    outStats.messagesDelivered.store(srcStats.messagesDelivered.load(std::memory_order_relaxed), std::memory_order_relaxed);
    outStats.messagesDropped.store(srcStats.messagesDropped.load(std::memory_order_relaxed), std::memory_order_relaxed);
    outStats.messagesExpired.store(srcStats.messagesExpired.load(std::memory_order_relaxed), std::memory_order_relaxed);
    outStats.bytesTransferred.store(srcStats.bytesTransferred.load(std::memory_order_relaxed), std::memory_order_relaxed);
    outStats.lastPublishTime.store(srcStats.lastPublishTime.load(std::memory_order_relaxed), std::memory_order_relaxed);
    outStats.lastDeliveryTime.store(srcStats.lastDeliveryTime.load(std::memory_order_relaxed), std::memory_order_relaxed);
    
    return true;
}

// Get topic by name
TopicId MessageBroker::getTopicByName(const char* name) const noexcept {
    if (!name) return INVALID_TOPIC_ID;
    
    // PSYCHOTIC: Linear search for now, in production use name->id map
    for (auto it = topics.begin(); it != topics.end(); ++it) {
        if (std::strcmp(it->second->name, name) == 0) {
            return it->first;
        }
    }
    
    return INVALID_TOPIC_ID;
}

// Clear all dead letters
void MessageBroker::clearDeadLetters() noexcept {
    MessageEnvelope envelope;
    while (deadLetterQueue.try_pop(envelope)) {
        // Discard
    }
    stats.deadLetterCount.store(0, std::memory_order_relaxed);
}

// Reset broker state
void MessageBroker::reset() noexcept {
    // Clear all topics
    for (auto it = topics.begin(); it != topics.end(); ++it) {
        delete it->second->buffer;
        delete it->second;
    }
    topics.clear();
    
    // Clear subscribers
    subscribers.clear();
    
    // Clear dead letters
    clearDeadLetters();
    
    // Reset counters
    nextTopicId.store(1, std::memory_order_relaxed);
    nextSubscriptionId.store(1, std::memory_order_relaxed);
    
    // Reset stats
    stats.totalMessagesRouted.store(0, std::memory_order_relaxed);
    stats.totalMessagesDropped.store(0, std::memory_order_relaxed);
    stats.totalBytesTransferred.store(0, std::memory_order_relaxed);
    stats.deadLetterCount.store(0, std::memory_order_relaxed);
}

// Internal: Deliver message to subscriber
bool MessageBroker::deliverToSubscriber(const Message& msg, const SubscriberInfo& subscriber) noexcept {
    // Check message type filter
    u32 msgType = msg.header.messageType;
    if ((subscriber.handler.filterMask & (1 << (msgType & 31))) == 0) {
        return false;  // Message filtered out
    }
    
    // Check target node filter
    if (subscriber.handler.targetNode != INVALID_NODE_ID) {
        if (msg.header.targetNode != subscriber.handler.targetNode.value) {
            return false;  // Not for this node
        }
    }
    
    // Call handler
    if (subscriber.handler.handler) {
        subscriber.handler.handler(msg, subscriber.handler.context);
        
        // Update stats (cast away const for stats update)
        const_cast<SubscriberInfo&>(subscriber).messagesReceived.fetch_add(1, std::memory_order_relaxed);
        const_cast<SubscriberInfo&>(subscriber).lastDeliveryTime.store(createTimestamp(), std::memory_order_relaxed);
        
        return true;
    }
    
    return false;
}

// Internal: Check if message is expired
bool MessageBroker::isMessageExpired(const MessageEnvelope& envelope) const noexcept {
    if (envelope.expiryTime == 0) {
        return false;  // No expiry
    }
    
    return createTimestamp() > envelope.expiryTime;
}

// Internal: Update topic statistics
void MessageBroker::updateTopicStats(TopicInfo* topic, bool delivered, u64 bytes) noexcept {
    if (!topic) return;
    
    if (delivered) {
        topic->stats.messagesDelivered.fetch_add(1, std::memory_order_relaxed);
        topic->stats.bytesTransferred.fetch_add(bytes, std::memory_order_relaxed);
        topic->stats.lastDeliveryTime.store(createTimestamp(), std::memory_order_relaxed);
    } else {
        topic->stats.messagesDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

AARENDOCORE_NAMESPACE_END