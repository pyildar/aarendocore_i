//===--- Core_StreamMultiplexer.cpp - N-to-K Stream Management ----------===//
//
// COMPILATION LEVEL: 6 (After MessageTypes, StreamSynchronizer)
// ORIGIN: Implementation of Core_StreamMultiplexer.h
//
// PSYCHOTIC PRECISION: ZERO-COPY, LOCK-FREE N→K ROUTING
//===----------------------------------------------------------------------===//

#include "Core_StreamMultiplexer.h"
#include <immintrin.h>  // For SIMD operations

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// STREAM MULTIPLEXER IMPLEMENTATION
// ============================================================================

// Constructor
StreamMultiplexer::StreamMultiplexer() noexcept 
    : inputBuffers(nullptr)
    , outputBuffers(nullptr)
    , mapping()
    , transformRules()
    , synchronizer(nullptr)
    , interpolator()
    , stats{0, 0, 0} {
    
    // PSYCHOTIC: Pre-allocate all buffers
    inputBuffers = reinterpret_cast<StreamBuffer*>(
        _aligned_malloc(sizeof(StreamBuffer) * StreamMapping::MAX_INPUT_STREAMS, 
                       AARENDOCORE_CACHE_LINE_SIZE));
    
    outputBuffers = reinterpret_cast<StreamBuffer*>(
        _aligned_malloc(sizeof(StreamBuffer) * StreamMapping::MAX_OUTPUT_STREAMS,
                       AARENDOCORE_CACHE_LINE_SIZE));
    
    // Placement new for each buffer
    if (inputBuffers) {
        for (u32 i = 0; i < StreamMapping::MAX_INPUT_STREAMS; ++i) {
            new (&inputBuffers[i]) StreamBuffer();
        }
    }
    
    if (outputBuffers) {
        for (u32 i = 0; i < StreamMapping::MAX_OUTPUT_STREAMS; ++i) {
            new (&outputBuffers[i]) StreamBuffer();
        }
    }
}

// Destructor
StreamMultiplexer::~StreamMultiplexer() noexcept {
    // Call destructors
    if (inputBuffers) {
        for (u32 i = 0; i < StreamMapping::MAX_INPUT_STREAMS; ++i) {
            inputBuffers[i].~StreamBuffer();
        }
        _aligned_free(inputBuffers);
    }
    
    if (outputBuffers) {
        for (u32 i = 0; i < StreamMapping::MAX_OUTPUT_STREAMS; ++i) {
            outputBuffers[i].~StreamBuffer();
        }
        _aligned_free(outputBuffers);
    }
}

// Configure mapping from input to output
void StreamMultiplexer::configureMapping(u32 input, u32 output) noexcept {
    if (input >= StreamMapping::MAX_INPUT_STREAMS || 
        output >= StreamMapping::MAX_OUTPUT_STREAMS) {
        return;
    }
    
    mapping.matrix.connect(input, output);
    
    // Update active counts
    u32 currentInput = mapping.activeInputCount.load(std::memory_order_relaxed);
    if (input >= currentInput) {
        mapping.activeInputCount.store(input + 1, std::memory_order_release);
    }
    
    u32 currentOutput = mapping.activeOutputCount.load(std::memory_order_relaxed);
    if (output >= currentOutput) {
        mapping.activeOutputCount.store(output + 1, std::memory_order_release);
    }
}

// Remove mapping
void StreamMultiplexer::removeMapping(u32 input, u32 output) noexcept {
    if (input >= StreamMapping::MAX_INPUT_STREAMS || 
        output >= StreamMapping::MAX_OUTPUT_STREAMS) {
        return;
    }
    
    mapping.matrix.disconnect(input, output);
}

// Add transformation rule
void StreamMultiplexer::addTransformRule(const TransformationRule& rule) noexcept {
    transformRules.insert(std::make_pair(rule.outputStream, rule));
}

// Remove transformation rule
void StreamMultiplexer::removeTransformRule(u32 outputStream) noexcept {
    transformRules.erase(outputStream);
}

// Push message to input stream
bool StreamMultiplexer::pushInput(u32 streamId, const Message& msg) noexcept {
    if (!inputBuffers || streamId >= StreamMapping::MAX_INPUT_STREAMS) {
        stats.messagesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Update interpolation state
    interpolator.updateState(streamId, msg);
    
    // Write to input buffer
    if (!inputBuffers[streamId].write(msg)) {
        stats.messagesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    return true;
}

// Pull message from output stream
bool StreamMultiplexer::pullOutput(u32 streamId, Message& msg) noexcept {
    if (!outputBuffers || streamId >= StreamMapping::MAX_OUTPUT_STREAMS) {
        return false;
    }
    
    return outputBuffers[streamId].read(msg);
}

// Main processing loop
void StreamMultiplexer::process() noexcept {
    if (!inputBuffers || !outputBuffers) return;
    
    // Process all active output streams
    u32 activeOutputs = mapping.activeOutputCount.load(std::memory_order_acquire);
    
    for (u32 output = 0; output < activeOutputs; ++output) {
        processStream(output);
    }
}

// Process single output stream
void StreamMultiplexer::processStream(u32 outputStream) noexcept {
    if (!inputBuffers || !outputBuffers) return;
    if (outputStream >= StreamMapping::MAX_OUTPUT_STREAMS) return;
    
    // Check for transformation rule
    tbb::concurrent_hash_map<u32, TransformationRule>::const_accessor accessor;
    if (transformRules.find(accessor, outputStream)) {
        processWithTransform(outputStream, accessor->second);
    } else {
        processPassthrough(outputStream);
    }
}

// Process passthrough (no transformation)
void StreamMultiplexer::processPassthrough(u32 output) noexcept {
    // PSYCHOTIC: Process all connected inputs
    for (u32 input = 0; input < StreamMapping::MAX_INPUT_STREAMS; ++input) {
        if (!mapping.matrix.isConnected(input, output)) continue;
        
        Message msg;
        while (inputBuffers[input].read(msg)) {
            if (outputBuffers[output].write(msg)) {
                stats.messagesRouted.fetch_add(1, std::memory_order_relaxed);
            } else {
                // Output buffer full, push back to input
                inputBuffers[input].write(msg);
                break;
            }
        }
    }
}

// Process with transformation
void StreamMultiplexer::processWithTransform(u32 output, const TransformationRule& rule) noexcept {
    switch (rule.type) {
        case TransformationType::PASSTHROUGH:
            processPassthrough(output);
            break;
            
        case TransformationType::INTERPOLATE:
            processInterpolation(output, rule);
            break;
            
        case TransformationType::AGGREGATE:
            processAggregation(output, rule);
            break;
            
        case TransformationType::SYNCHRONIZE:
            processSynchronization(output, rule);
            break;
            
        default:
            // Other transformations not implemented yet
            processPassthrough(output);
            break;
    }
    
    stats.transformsApplied.fetch_add(1, std::memory_order_relaxed);
}

// Process interpolation transformation
void StreamMultiplexer::processInterpolation(u32 output, const TransformationRule& rule) noexcept {
    // PSYCHOTIC: Time-aligned interpolation across streams
    u64 targetTime = createTimestamp();
    
    for (u32 i = 0; i < 8 && rule.inputStreams[i] != 0xFFFFFFFF; ++i) {
        u32 input = rule.inputStreams[i];
        if (input >= StreamMapping::MAX_INPUT_STREAMS) continue;
        
        const InterpolationEngine::StreamState* state = interpolator.getState(input);
        if (!state || !state->hasData) continue;
        
        Message msg1, msg2;
        if (inputBuffers[input].read(msg1)) {
            // Check if we have two messages for interpolation
            if (inputBuffers[input].read(msg2)) {
                Message interpolated = interpolator.interpolateLinear(msg1, msg2, targetTime);
                
                if (outputBuffers[output].write(interpolated)) {
                    stats.messagesRouted.fetch_add(1, std::memory_order_relaxed);
                }
                
                // Push second message back
                inputBuffers[input].write(msg2);
            } else {
                // Only one message, use as-is
                if (outputBuffers[output].write(msg1)) {
                    stats.messagesRouted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }
}

// Process aggregation transformation
void StreamMultiplexer::processAggregation(u32 output, const TransformationRule& rule) noexcept {
    // PSYCHOTIC: Aggregate multiple streams into one
    Message aggregated;
    aggregated.header.messageType = static_cast<u32>(MessageType::AGGREGATED_DATA);
    aggregated.header.timestamp = createTimestamp();
    aggregated.header.sourceNode = 0;  // Will be set by DAG
    aggregated.header.targetNode = static_cast<u16>(output);
    
    f64 sumPrice = 0.0;
    f64 sumVolume = 0.0;
    u32 count = 0;
    
    // Collect data from all input streams
    for (u32 i = 0; i < 8 && rule.inputStreams[i] != 0xFFFFFFFF; ++i) {
        u32 input = rule.inputStreams[i];
        if (input >= StreamMapping::MAX_INPUT_STREAMS) continue;
        
        Message msg;
        if (inputBuffers[input].read(msg)) {
            // Extract values based on message type
            switch (static_cast<MessageType>(msg.header.messageType)) {
                case MessageType::TICK_DATA:
                    sumPrice += msg.tick.price;
                    sumVolume += msg.tick.volume;
                    count++;
                    break;
                    
                case MessageType::BAR_DATA:
                    sumPrice += msg.bar.close;
                    sumVolume += msg.bar.volume;
                    count++;
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    // Create aggregated message
    if (count > 0) {
        aggregated.aggregated.value1 = sumPrice / count;  // Average price
        aggregated.aggregated.value2 = sumVolume;         // Total volume
        aggregated.aggregated.count = count;
        aggregated.aggregated.aggregationType = rule.params.aggregate.windowType;
        
        if (outputBuffers[output].write(aggregated)) {
            stats.messagesRouted.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// Process synchronization transformation
void StreamMultiplexer::processSynchronization(u32 output, const TransformationRule& rule) noexcept {
    if (!synchronizer) {
        // Fall back to passthrough if no synchronizer
        processPassthrough(output);
        return;
    }
    
    // PSYCHOTIC: Use StreamSynchronizer for time alignment
    // Update synchronizer with all input stream ticks
    for (u32 i = 0; i < 8 && rule.inputStreams[i] != 0xFFFFFFFF; ++i) {
        u32 input = rule.inputStreams[i];
        if (input >= StreamMapping::MAX_INPUT_STREAMS) continue;
        
        Message msg;
        while (inputBuffers[input].read(msg)) {
            // Update synchronizer based on message type
            if (static_cast<MessageType>(msg.header.messageType) == MessageType::TICK_DATA) {
                // Convert Message tick to Tick for synchronizer
                // PSYCHOTIC: Tick struct has ONLY: timestamp, price, volume, flags!
                Tick tick;
                tick.timestamp = msg.header.timestamp;
                tick.price = msg.tick.price;
                tick.volume = msg.tick.volume;
                tick.flags = 0;  // Set flags from message context
                
                synchronizer->updateStream(input, tick);
            } else if (static_cast<MessageType>(msg.header.messageType) == MessageType::BAR_DATA) {
                // Convert Message bar to Bar for synchronizer
                // PSYCHOTIC: Bar struct has: timestamp, OHLCV, tickCount, padding!
                Bar bar;
                bar.timestamp = msg.header.timestamp;
                bar.open = msg.bar.open;
                bar.high = msg.bar.high;
                bar.low = msg.bar.low;
                bar.close = msg.bar.close;
                bar.volume = msg.bar.volume;
                bar.tickCount = 0;  // Not available in BarMessage
                // NO vwap field in Bar struct!
                
                synchronizer->updateBar(input, bar);
            }
        }
    }
    
    // Get synchronized output
    SynchronizedOutput syncOutput;
    if (synchronizer->synchronize(syncOutput)) {
        // Convert synchronized ticks back to messages
        for (u32 i = 0; i < syncOutput.streamCount; ++i) {
            Message syncedMsg;
            syncedMsg.header.messageType = static_cast<u32>(MessageType::TICK_DATA);
            syncedMsg.header.timestamp = syncOutput.syncedTicks[i].timestamp;
            syncedMsg.header.sourceNode = static_cast<u16>(syncOutput.leaderStreamId);
            syncedMsg.header.targetNode = static_cast<u16>(output);
            
            // PSYCHOTIC: TickMessage has symbolId, exchangeId, price, volume, bid, ask, reserved
            // But Tick struct only has timestamp, price, volume, flags!
            // We must use what we have from the synchronized Tick
            syncedMsg.tick.symbolId = i;  // Use stream index as symbol ID  
            syncedMsg.tick.exchangeId = 0;  // Default exchange
            syncedMsg.tick.price = syncOutput.syncedTicks[i].price;
            syncedMsg.tick.volume = syncOutput.syncedTicks[i].volume;
            syncedMsg.tick.bid = syncOutput.syncedTicks[i].price;  // Use price as bid
            syncedMsg.tick.ask = syncOutput.syncedTicks[i].price;  // Use price as ask
            syncedMsg.tick.reserved = 0;
            
            if (outputBuffers[output].write(syncedMsg)) {
                stats.messagesRouted.fetch_add(1, std::memory_order_relaxed);
            } else {
                // Buffer full, stop processing
                break;
            }
        }
    }
}

AARENDOCORE_NAMESPACE_END