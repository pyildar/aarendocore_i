//===--- Core_StreamMultiplexer.h - N-to-K Stream Management ------------===//
//
// COMPILATION LEVEL: 6 (After MessageTypes, StreamSynchronizer)
// DEPENDENCIES: 
//   - Core_Types.h (u32, u64, AtomicU32)
//   - Core_MessageTypes.h (Message, MessageType)
//   - Core_StreamSynchronizer.h (StreamSynchronizer)
//   - Core_DAGTypes.h (TransformationType)
// ORIGIN: NEW - Handles N input streams to K output streams
//
// PSYCHOTIC PRECISION: ZERO-COPY, LOCK-FREE N→K ROUTING
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_STREAMMULTIPLEXER_H
#define AARENDOCORE_CORE_STREAMMULTIPLEXER_H

#include "Core_Platform.h"
#include "Core_Types.h"
#include "Core_MessageTypes.h"
#include "Core_StreamSynchronizer.h"
#include "Core_DAGTypes.h"
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// STREAM MAPPING CONFIGURATION
// ============================================================================
// Origin: Configuration for N→K stream mapping
// Scope: Defines how input streams map to output streams
struct StreamMapping {
    // Maximum supported streams
    static constexpr u32 MAX_INPUT_STREAMS = 256;   // N ≤ 256
    static constexpr u32 MAX_OUTPUT_STREAMS = 128;  // K ≤ 128
    
    // Mapping matrix: which inputs go to which outputs
    // PSYCHOTIC: Bit matrix for zero-allocation mapping
    struct MappingMatrix {
        // Each output has a bitmask of its input streams
        // 256 bits = 4 x u64 per output stream
        u64 inputMask[MAX_OUTPUT_STREAMS][4];  // 256 bits per output
        
        // Default constructor - zero initialize
        MappingMatrix() noexcept {
            for (u32 i = 0; i < MAX_OUTPUT_STREAMS; ++i) {
                for (u32 j = 0; j < 4; ++j) {
                    inputMask[i][j] = 0;
                }
            }
        }
        
        // Check if input i goes to output j
        bool isConnected(u32 input, u32 output) const noexcept {
            if (input >= MAX_INPUT_STREAMS || output >= MAX_OUTPUT_STREAMS) return false;
            u32 wordIdx = input / 64;
            u32 bitIdx = input % 64;
            return (inputMask[output][wordIdx] & (1ULL << bitIdx)) != 0;
        }
        
        // Connect input i to output j
        void connect(u32 input, u32 output) noexcept {
            if (input >= MAX_INPUT_STREAMS || output >= MAX_OUTPUT_STREAMS) return;
            u32 wordIdx = input / 64;
            u32 bitIdx = input % 64;
            inputMask[output][wordIdx] |= (1ULL << bitIdx);
        }
        
        // Disconnect input i from output j
        void disconnect(u32 input, u32 output) noexcept {
            if (input >= MAX_INPUT_STREAMS || output >= MAX_OUTPUT_STREAMS) return;
            u32 wordIdx = input / 64;
            u32 bitIdx = input % 64;
            inputMask[output][wordIdx] &= ~(1ULL << bitIdx);
        }
    };
    
    MappingMatrix matrix;
    AtomicU32 activeInputCount;
    AtomicU32 activeOutputCount;
    
    // Constructor
    StreamMapping() noexcept 
        : matrix()
        , activeInputCount(0)
        , activeOutputCount(0) {}
};

// ============================================================================
// STREAM TRANSFORMATION RULES
// ============================================================================
// Origin: Rules for transforming data between streams
// Scope: Defines how data is transformed during routing
struct TransformationRule {
    TransformationType type;     // Type of transformation
    u32 inputStreams[8];         // Up to 8 input streams
    u32 outputStream;            // Target output stream
    u32 priority;                // Rule priority
    
    // Transformation parameters
    union TransformParams {
        struct { u32 windowSize; u32 windowType; } aggregate;
        struct { f64 cutoffFreq; u32 filterType; } filter;
        struct { u32 targetRate; u32 method; } interpolate;
        struct { u64 syncWindowNs; u32 mode; } synchronize;
        
        // Default constructor
        TransformParams() noexcept : aggregate{0, 0} {}
    } params;
    
    // Constructor
    TransformationRule() noexcept 
        : type(TransformationType::PASSTHROUGH)
        , outputStream(0)
        , priority(0)
        , params() {
        for (u32 i = 0; i < 8; ++i) {
            inputStreams[i] = 0xFFFFFFFF;  // Invalid stream ID
        }
    }
};

// ============================================================================
// STREAM BUFFER - Lock-free circular buffer for each stream
// ============================================================================
// Origin: Buffer for stream data
// Scope: Each stream has its own buffer
struct StreamBuffer {
    static constexpr u32 BUFFER_SIZE = 65536;  // 64K messages = 4MB per buffer
    
    alignas(AARENDOCORE_CACHE_LINE_SIZE) Message buffer[BUFFER_SIZE];
    alignas(AARENDOCORE_CACHE_LINE_SIZE) AtomicU32 writePos;
    alignas(AARENDOCORE_CACHE_LINE_SIZE) AtomicU32 readPos;
    
    // Constructor
    StreamBuffer() noexcept 
        : writePos(0)
        , readPos(0) {
        // Messages are default-constructed
    }
    
    // Lock-free write
    bool write(const Message& msg) noexcept {
        u32 pos = writePos.load(std::memory_order_acquire);
        u32 nextPos = (pos + 1) % BUFFER_SIZE;
        u32 currentRead = readPos.load(std::memory_order_acquire);
        
        if (nextPos == currentRead) return false;  // Buffer full
        
        buffer[pos] = msg;
        writePos.store(nextPos, std::memory_order_release);
        return true;
    }
    
    // Lock-free read
    bool read(Message& msg) noexcept {
        u32 pos = readPos.load(std::memory_order_acquire);
        u32 currentWrite = writePos.load(std::memory_order_acquire);
        
        if (pos == currentWrite) return false;  // Buffer empty
        
        msg = buffer[pos];
        readPos.store((pos + 1) % BUFFER_SIZE, std::memory_order_release);
        return true;
    }
    
    // Check if empty
    bool empty() const noexcept {
        return readPos.load(std::memory_order_relaxed) == 
               writePos.load(std::memory_order_relaxed);
    }
    
    // Check if full
    bool full() const noexcept {
        u32 nextWrite = (writePos.load(std::memory_order_relaxed) + 1) % BUFFER_SIZE;
        return nextWrite == readPos.load(std::memory_order_relaxed);
    }
    
    // Get count (approximate)
    u32 count() const noexcept {
        u32 w = writePos.load(std::memory_order_relaxed);
        u32 r = readPos.load(std::memory_order_relaxed);
        return (w >= r) ? (w - r) : (BUFFER_SIZE - r + w);
    }
};

// ============================================================================
// INTERPOLATION ENGINE - For time alignment
// ============================================================================
// Origin: Engine for interpolating data between time points
// Scope: Used for time-aligning streams
class InterpolationEngine {
public:
    // Interpolation methods
    enum class Method : u32 {
        LINEAR = 0,
        CUBIC = 1,
        SPLINE = 2,
        NEAREST = 3
    };
    
    // Stream state for interpolation
    struct StreamState {
        Message lastMessage;
        u64 lastTimestamp;
        f64 lastValue;
        bool hasData;
        
        StreamState() noexcept 
            : lastMessage()
            , lastTimestamp(0)
            , lastValue(0.0)
            , hasData(false) {}
    };
    
private:
    StreamState states[StreamMapping::MAX_INPUT_STREAMS];
    
public:
    // Linear interpolation between two messages
    Message interpolateLinear(const Message& m1, const Message& m2, u64 targetTime) noexcept {
        Message result = m1;
        
        if (m1.header.timestamp == m2.header.timestamp) {
            return m1;  // No interpolation needed
        }
        
        f64 factor = static_cast<f64>(targetTime - m1.header.timestamp) / 
                    static_cast<f64>(m2.header.timestamp - m1.header.timestamp);
        
        // PSYCHOTIC: Type-specific interpolation
        switch (static_cast<MessageType>(m1.header.messageType)) {
            case MessageType::TICK_DATA: {
                result.tick.price = m1.tick.price + factor * (m2.tick.price - m1.tick.price);
                result.tick.volume = m1.tick.volume + factor * (m2.tick.volume - m1.tick.volume);
                result.tick.bid = m1.tick.bid + factor * (m2.tick.bid - m1.tick.bid);
                result.tick.ask = m1.tick.ask + factor * (m2.tick.ask - m1.tick.ask);
                break;
            }
            case MessageType::BAR_DATA: {
                // Bars don't interpolate - use nearest
                result = (factor < 0.5) ? m1 : m2;
                break;
            }
            default:
                // Generic interpolation for other types
                break;
        }
        
        result.header.timestamp = targetTime;
        return result;
    }
    
    // Update state for a stream
    void updateState(u32 streamId, const Message& msg) noexcept {
        if (streamId >= StreamMapping::MAX_INPUT_STREAMS) return;
        
        states[streamId].lastMessage = msg;
        states[streamId].lastTimestamp = msg.header.timestamp;
        states[streamId].hasData = true;
        
        // Extract value based on message type
        switch (static_cast<MessageType>(msg.header.messageType)) {
            case MessageType::TICK_DATA:
                states[streamId].lastValue = msg.tick.price;
                break;
            case MessageType::BAR_DATA:
                states[streamId].lastValue = msg.bar.close;
                break;
            default:
                break;
        }
    }
    
    // Get state for a stream
    const StreamState* getState(u32 streamId) const noexcept {
        if (streamId >= StreamMapping::MAX_INPUT_STREAMS) return nullptr;
        return &states[streamId];
    }
};

// ============================================================================
// STREAM MULTIPLEXER - Main N→K routing engine
// ============================================================================
// Origin: Central multiplexer for stream routing
// Scope: Manages all stream routing and transformation
class StreamMultiplexer {
private:
    // Stream buffers
    StreamBuffer* inputBuffers;   // Dynamically allocated array
    StreamBuffer* outputBuffers;  // Dynamically allocated array
    
    // Routing configuration
    StreamMapping mapping;
    tbb::concurrent_hash_map<u32, TransformationRule> transformRules;
    
    // Synchronization
    StreamSynchronizer* synchronizer;
    
    // Interpolation engine
    InterpolationEngine interpolator;
    
    // Statistics
    struct Stats {
        AtomicU64 messagesRouted;
        AtomicU64 messagesDropped;
        AtomicU64 transformsApplied;
    } stats;
    
public:
    // Constructor
    StreamMultiplexer() noexcept;
    
    // Destructor
    ~StreamMultiplexer() noexcept;
    
    // Configuration
    void configureMapping(u32 input, u32 output) noexcept;
    void removeMapping(u32 input, u32 output) noexcept;
    void addTransformRule(const TransformationRule& rule) noexcept;
    void removeTransformRule(u32 outputStream) noexcept;
    void setSynchronizer(StreamSynchronizer* sync) noexcept { synchronizer = sync; }
    
    // Data flow
    bool pushInput(u32 streamId, const Message& msg) noexcept;
    bool pullOutput(u32 streamId, Message& msg) noexcept;
    
    // Processing
    void process() noexcept;
    void processStream(u32 outputStream) noexcept;
    
    // Statistics
    u64 getMessagesRouted() const noexcept { return stats.messagesRouted.load(); }
    u64 getMessagesDropped() const noexcept { return stats.messagesDropped.load(); }
    u64 getTransformsApplied() const noexcept { return stats.transformsApplied.load(); }
    
    // Stream info
    u32 getActiveInputCount() const noexcept { return mapping.activeInputCount.load(); }
    u32 getActiveOutputCount() const noexcept { return mapping.activeOutputCount.load(); }
    bool isInputConnected(u32 input, u32 output) const noexcept { 
        return mapping.matrix.isConnected(input, output); 
    }
    
private:
    // Processing helpers
    void processPassthrough(u32 output) noexcept;
    void processWithTransform(u32 output, const TransformationRule& rule) noexcept;
    void processInterpolation(u32 output, const TransformationRule& rule) noexcept;
    void processAggregation(u32 output, const TransformationRule& rule) noexcept;
    void processSynchronization(u32 output, const TransformationRule& rule) noexcept;
};

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_STREAMMULTIPLEXER_H