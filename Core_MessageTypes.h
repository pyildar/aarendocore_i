//===--- Core_MessageTypes.h - ALL Message Types ------------------------===//
//
// COMPILATION LEVEL: 3 (After Core_Types, Core_Config, Core_Alignment)
// DEPENDENCIES: 
//   - Core_Types.h (u32, u64, f64)
//   - Core_Alignment.h (alignas)
// ORIGIN: NEW - Defines ALL data types passing between units
//
// PSYCHOTIC PRECISION: EVERY MESSAGE EXACTLY 64 BYTES
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_MESSAGETYPES_H
#define AARENDOCORE_CORE_MESSAGETYPES_H

#include "Core_Platform.h"   // LEVEL 0: Platform macros
#include "Core_Types.h"      // LEVEL 1: Basic types (u8, u16, u32, u64, f64)
#include "Core_Alignment.h"  // LEVEL 2: Alignment macros

#ifdef _MSC_VER
#include <intrin.h>  // For __rdtsc() intrinsic
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// BASE MESSAGE HEADER - 16 bytes
// ============================================================================
// Origin: Common header for all messages
// Scope: Used by every message type for routing
struct MessageHeader {
    u64 timestamp;      // RDTSC timestamp of creation
    u32 messageType;    // MessageType enum below
    u16 sourceNode;     // Source DAG node ID (max 65535 nodes)
    u16 targetNode;     // Target DAG node ID (max 65535 nodes)
};

// ============================================================================
// MESSAGE TYPE ENUMERATION - COMPLETE LIST
// ============================================================================
enum class MessageType : u32 {
    // === MARKET DATA MESSAGES (0x1000) ===
    TICK_DATA           = 0x1001,  // Single tick
    BAR_DATA            = 0x1002,  // OHLCV bar
    DEPTH_DATA          = 0x1003,  // Order book depth
    TRADE_DATA          = 0x1004,  // Executed trade
    
    // === NORMALIZED DATA (0x2000) ===
    NORMALIZED_TICK     = 0x2001,  // After normalization
    NORMALIZED_BAR      = 0x2002,  // After bar aggregation
    INTERPOLATED_DATA   = 0x2003,  // After interpolation
    ALIGNED_DATA        = 0x2004,  // After time alignment
    
    // === COMPUTED VALUES (0x3000) ===
    INDICATOR_VALUE     = 0x3001,  // Technical indicator
    STATISTIC_VALUE     = 0x3002,  // Statistical measure
    ML_PREDICTION       = 0x3003,  // ML model output
    PATTERN_MATCH       = 0x3004,  // Pattern detection
    AGGREGATED_DATA     = 0x3005,  // Aggregated multi-stream data
    
    // === SIGNALS & DECISIONS (0x4000) ===
    TRADING_SIGNAL      = 0x4001,  // Buy/sell signal
    RISK_ASSESSMENT     = 0x4002,  // Risk metrics
    POSITION_SIZE       = 0x4003,  // Position sizing
    ALERT_MESSAGE       = 0x4004,  // Alert/notification
    
    // === CONTROL MESSAGES (0x5000) ===
    START_PROCESSING    = 0x5001,  // Start command
    STOP_PROCESSING     = 0x5002,  // Stop command
    FLUSH_BUFFERS       = 0x5003,  // Flush data
    SYNCHRONIZE         = 0x5004,  // Sync barrier
    
    // === ERROR MESSAGES (0x6000) ===
    ERROR_DATA          = 0x6001,  // Data error
    ERROR_PROCESSING    = 0x6002,  // Processing error
    ERROR_TIMEOUT       = 0x6003,  // Timeout error
    ERROR_OVERFLOW      = 0x6004   // Buffer overflow
};

// Bar structure is already defined in Core_Types.h (line 134)
// It's a 64-byte aligned structure with timestamp, OHLCV data, volume, and tick count

// ============================================================================
// TICK MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) TickMessage {
    MessageHeader header;     // 16 bytes
    u32 symbolId;            // 4 bytes - Symbol identifier
    u32 exchangeId;          // 4 bytes - Exchange identifier
    f64 price;               // 8 bytes - Price value
    f64 volume;              // 8 bytes - Volume
    f64 bid;                 // 8 bytes - Best bid
    f64 ask;                 // 8 bytes - Best ask
    // Total so far: 16 + 4 + 4 + 8 + 8 + 8 + 8 = 56 bytes
    // Need 8 more bytes for padding
    u64 reserved;            // 8 bytes - Reserved/padding
};
// static_assert(sizeof(TickMessage) == 64, "TickMessage must be exactly 64 bytes");

// ============================================================================
// BAR MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) BarMessage {
    MessageHeader header;     // 16 bytes
    u32 symbolId;            // 4 bytes
    u32 period;              // 4 bytes - Bar period in seconds
    f64 open;                // 8 bytes
    f64 high;                // 8 bytes
    f64 low;                 // 8 bytes
    f64 close;               // 8 bytes
    f64 volume;              // 8 bytes
};
// static_assert(sizeof(BarMessage) == 64, "BarMessage must be exactly 64 bytes");

// ============================================================================
// INTERPOLATED MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) InterpolatedMessage {
    MessageHeader header;     // 16 bytes
    u32 streamId;            // 4 bytes - Stream identifier
    u32 interpolationType;   // 4 bytes - Linear/Cubic/Spline
    f64 originalTime;        // 8 bytes - Original timestamp
    f64 interpolatedTime;    // 8 bytes - Target timestamp
    f64 value;               // 8 bytes - Interpolated value
    f64 confidence;          // 8 bytes - Confidence score
    u64 sourcePoints;        // 8 bytes - Bitmap of source points used
};
static_assert(sizeof(InterpolatedMessage) == 64, "InterpolatedMessage must be exactly 64 bytes");

// ============================================================================
// SIGNAL MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) SignalMessage {
    MessageHeader header;     // 16 bytes
    u32 strategyId;          // 4 bytes - Strategy identifier
    u32 signalType;          // 4 bytes - Buy/Sell/Hold
    f64 entryPrice;          // 8 bytes
    f64 stopLoss;            // 8 bytes
    f64 takeProfit;          // 8 bytes
    f64 confidence;          // 8 bytes - Signal confidence
    u64 metadata;            // 8 bytes - Additional data
};
static_assert(sizeof(SignalMessage) == 64, "SignalMessage must be exactly 64 bytes");

// ============================================================================
// INDICATOR MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) IndicatorMessage {
    MessageHeader header;     // 16 bytes
    u32 indicatorType;       // 4 bytes - SMA/EMA/RSI/etc
    u32 period;              // 4 bytes - Calculation period
    f64 value;               // 8 bytes - Indicator value
    f64 upperBand;           // 8 bytes - Upper band (Bollinger, etc)
    f64 lowerBand;           // 8 bytes - Lower band
    f64 signal;              // 8 bytes - Signal line (MACD, etc)
    u64 flags;               // 8 bytes - Additional flags
};
static_assert(sizeof(IndicatorMessage) == 64, "IndicatorMessage must be exactly 64 bytes");

// ============================================================================
// ERROR MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) ErrorMessage {
    MessageHeader header;     // 16 bytes
    u32 errorCode;           // 4 bytes - Error code
    u32 severity;            // 4 bytes - Error severity
    u64 sourceLocation;      // 8 bytes - Source code location
    u64 contextData;         // 8 bytes - Context-specific data
    char description[24];    // 24 bytes - Short error description
};
static_assert(sizeof(ErrorMessage) == 64, "ErrorMessage must be exactly 64 bytes");

// ============================================================================
// CONTROL MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) ControlMessage {
    MessageHeader header;     // 16 bytes
    u32 command;             // 4 bytes - Command type
    u32 flags;               // 4 bytes - Command flags
    u64 parameter1;          // 8 bytes - Parameter 1
    u64 parameter2;          // 8 bytes - Parameter 2
    u64 parameter3;          // 8 bytes - Parameter 3
    u64 parameter4;          // 8 bytes - Parameter 4
    u64 reserved;            // 8 bytes - Reserved for future use
};
static_assert(sizeof(ControlMessage) == 64, "ControlMessage must be exactly 64 bytes");

// ============================================================================
// AGGREGATED MESSAGE - EXACTLY 64 bytes
// ============================================================================
struct alignas(64) AggregatedMessage {
    MessageHeader header;     // 16 bytes
    u32 aggregationType;     // 4 bytes - Type of aggregation
    u32 count;               // 4 bytes - Number of messages aggregated
    f64 value1;              // 8 bytes - First aggregated value (e.g., average price)
    f64 value2;              // 8 bytes - Second aggregated value (e.g., total volume)
    f64 value3;              // 8 bytes - Third aggregated value
    f64 value4;              // 8 bytes - Fourth aggregated value
    u64 reserved;            // 8 bytes - Reserved for future use
};
static_assert(sizeof(AggregatedMessage) == 64, "AggregatedMessage must be exactly 64 bytes");

// ============================================================================
// GENERIC MESSAGE UNION - EXACTLY 64 bytes
// ============================================================================
// Origin: Union for type-safe message passing
// Scope: Used throughout message broker for zero-copy routing
union Message {
    MessageHeader header;
    TickMessage tick;
    BarMessage bar;
    InterpolatedMessage interpolated;
    SignalMessage signal;
    IndicatorMessage indicator;
    ErrorMessage error;
    ControlMessage control;
    AggregatedMessage aggregated;
    u8 raw[64];  // Raw bytes for custom messages
};
// static_assert(sizeof(Message) == 64, "Message union must be exactly 64 bytes");

// ============================================================================
// MESSAGE BUFFER - For batch operations
// ============================================================================
// Origin: Buffer for batch message processing
// Scope: Used by processing units for batch operations
struct alignas(AARENDOCORE_CACHE_LINE_SIZE) MessageBuffer {
    static constexpr u32 BUFFER_SIZE = 1024;  // 1024 messages = 64KB
    
    Message messages[BUFFER_SIZE];  // Fixed-size buffer
    AtomicU32 writePos;             // Write position
    AtomicU32 readPos;              // Read position
    AtomicU32 count;                // Current message count
    
    // Lock-free write
    bool write(const Message& msg) noexcept {
        u32 pos = writePos.load(std::memory_order_acquire);
        u32 nextPos = (pos + 1) % BUFFER_SIZE;
        u32 currentRead = readPos.load(std::memory_order_acquire);
        
        if (nextPos == currentRead) return false;  // Buffer full
        
        messages[pos] = msg;
        writePos.store(nextPos, std::memory_order_release);
        count.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    
    // Lock-free read
    bool read(Message& msg) noexcept {
        u32 pos = readPos.load(std::memory_order_acquire);
        u32 currentWrite = writePos.load(std::memory_order_acquire);
        
        if (pos == currentWrite) return false;  // Buffer empty
        
        msg = messages[pos];
        readPos.store((pos + 1) % BUFFER_SIZE, std::memory_order_release);
        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    
    // Get current count
    u32 size() const noexcept {
        return count.load(std::memory_order_relaxed);
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
};

// ============================================================================
// MESSAGE ROUTING INFO
// ============================================================================
// Origin: Routing information for message broker
// Scope: Used by message broker for routing decisions
struct MessageRoute {
    u16 sourceNode;          // Source node ID
    u16 targetNode;          // Target node ID
    MessageType messageType;  // Type of message
    u32 priority;            // Routing priority
    u64 routeFlags;          // Routing flags
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Get message type from generic message
inline MessageType getMessageType(const Message& msg) noexcept {
    return static_cast<MessageType>(msg.header.messageType);
}

// Create timestamp using RDTSC
inline u64 createTimestamp() noexcept {
    // PSYCHOTIC: RDTSC for nanosecond precision
#ifdef _MSC_VER
    // MSVC intrinsic
    return __rdtsc();
#else
    // GCC/Clang inline assembly
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return (static_cast<u64>(hi) << 32) | lo;
#endif
}

// Initialize message header
inline void initMessageHeader(MessageHeader& header, MessageType type, 
                             u16 source = 0, u16 target = 0) noexcept {
    header.timestamp = createTimestamp();
    header.messageType = static_cast<u32>(type);
    header.sourceNode = source;
    header.targetNode = target;
}

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCORE_CORE_MESSAGETYPES_H