//===--- Core_BaseProcessingUnit.cpp - Base Processing Unit Impl --------===//
//
// COMPILATION LEVEL: 3
// ORIGIN: Implementation for Core_BaseProcessingUnit.h
// DEPENDENCIES: Core_BaseProcessingUnit.h, Core_NUMA.h
// DEPENDENTS: None
//
// Common implementation for all processing units.
//===----------------------------------------------------------------------===//

#include "Core_BaseProcessingUnit.h"
#include <chrono>

namespace AARendoCoreGLM {

// ==========================================================================
// CONSTRUCTOR/DESTRUCTOR
// ==========================================================================

// Origin: Constructor implementation
// Input: type - Unit type
//        capabilities - Capability bitfield
//        numaNode - NUMA affinity
BaseProcessingUnit::BaseProcessingUnit(ProcessingUnitType type, 
                                       u64 capabilities,
                                       i32 numaNode) noexcept
    : config_{}
    , metrics_{}
    , state_(static_cast<u8>(ProcessingUnitState::UNINITIALIZED))
    , capabilities_(capabilities)
    , type_(type)
    , connectedUnits_{}
    , connectedCount_(0)
    , numaNode_(numaNode)
    , padding_{} {
    
    // Initialize metrics to zero
    metrics_.ticksProcessed.store(0, std::memory_order_relaxed);
    metrics_.batchesProcessed.store(0, std::memory_order_relaxed);
    metrics_.bytesProcessed.store(0, std::memory_order_relaxed);
    metrics_.totalProcessingTimeNs.store(0, std::memory_order_relaxed);
    metrics_.minLatencyNs.store(UINT64_MAX, std::memory_order_relaxed);
    metrics_.maxLatencyNs.store(0, std::memory_order_relaxed);
    metrics_.errorCount.store(0, std::memory_order_relaxed);
    metrics_.skipCount.store(0, std::memory_order_relaxed);
    metrics_.queueDepth.store(0, std::memory_order_relaxed);
    metrics_.connectedUnits.store(0, std::memory_order_relaxed);
    metrics_.lastUpdateTimestamp.store(0, std::memory_order_relaxed);
}

// ==========================================================================
// LIFECYCLE METHODS
// ==========================================================================

// Origin: Initialize the processing unit
// Input: config - Configuration parameters
// Output: ResultCode
ResultCode BaseProcessingUnit::initialize(const ProcessingUnitConfig& config) noexcept {
    // Validate configuration
    if (!validateConfig(config)) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    // Transition to initializing state
    if (!transitionState(ProcessingUnitState::INITIALIZING)) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    // Store configuration
    config_ = config;
    
    // Set NUMA affinity if specified
    if (config.numaNode >= 0) {
        // NUMA binding would happen here
        // SetNumaNodeAffinity(config.numaNode);
    }
    
    // Transition to ready state
    if (!transitionState(ProcessingUnitState::READY)) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    return ResultCode::SUCCESS;
}

// Origin: Validate unit state
// Output: true if valid
bool BaseProcessingUnit::validate() const noexcept {
    // Get current state
    // currentState: Origin - Local from atomic load, Scope: function
    const auto currentState = static_cast<ProcessingUnitState>(
        state_.load(std::memory_order_acquire));
    
    // Check state is valid
    if (currentState == ProcessingUnitState::UNINITIALIZED ||
        currentState == ProcessingUnitState::ERROR ||
        currentState == ProcessingUnitState::TERMINATED) {
        return false;
    }
    
    // Check configuration is valid
    return validateConfig(config_);
}

// Origin: Shutdown the unit
// Output: ResultCode
ResultCode BaseProcessingUnit::shutdown() noexcept {
    // Transition to shutting down
    if (!transitionState(ProcessingUnitState::SHUTTING_DOWN)) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    // Disconnect from all connected units
    // count: Origin - Local from atomic load, Scope: function
    const u32 count = connectedCount_.load(std::memory_order_acquire);
    for (u32 i = 0; i < count; ++i) {
        disconnectFrom(connectedUnits_[i]);
    }
    
    // Reset metrics
    resetMetrics();
    
    // Transition to terminated
    if (!transitionState(ProcessingUnitState::TERMINATED)) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    return ResultCode::SUCCESS;
}

// ==========================================================================
// ROUTING METHODS
// ==========================================================================

// Origin: Connect to another unit
// Input: targetUnit - Target unit ID
//        connectionType - Connection type
// Output: ResultCode
ResultCode BaseProcessingUnit::connectTo(ProcessingUnitId targetUnit,
                                         [[maybe_unused]] u32 connectionType) noexcept {
    // Get current count
    // currentCount: Origin - Local from atomic load, Scope: function
    u32 currentCount = connectedCount_.load(std::memory_order_acquire);
    
    if (currentCount >= MAX_CONNECTED_UNITS) {
        return ResultCode::ERROR_CAPACITY_EXCEEDED;
    }
    
    // Check if already connected
    for (u32 i = 0; i < currentCount; ++i) {
        if (connectedUnits_[i] == targetUnit) {
            return ResultCode::ERROR_ALREADY_EXISTS;
        }
    }
    
    // Add connection
    connectedUnits_[currentCount] = targetUnit;
    connectedCount_.fetch_add(1, std::memory_order_release);
    metrics_.connectedUnits.fetch_add(1, std::memory_order_relaxed);
    
    return ResultCode::SUCCESS;
}

// Origin: Disconnect from another unit
// Input: targetUnit - Target unit ID
// Output: ResultCode
ResultCode BaseProcessingUnit::disconnectFrom(ProcessingUnitId targetUnit) noexcept {
    // Get current count
    // currentCount: Origin - Local from atomic load, Scope: function
    u32 currentCount = connectedCount_.load(std::memory_order_acquire);
    
    // Find and remove connection
    for (u32 i = 0; i < currentCount; ++i) {
        if (connectedUnits_[i] == targetUnit) {
            // Shift remaining connections
            for (u32 j = i; j < currentCount - 1; ++j) {
                connectedUnits_[j] = connectedUnits_[j + 1];
            }
            
            connectedCount_.fetch_sub(1, std::memory_order_release);
            metrics_.connectedUnits.fetch_sub(1, std::memory_order_relaxed);
            return ResultCode::SUCCESS;
        }
    }
    
    return ResultCode::ERROR_NOT_FOUND;
}

// Origin: Route data to connected units
// Input: data - Data to route
//        size - Data size
// Output: Number of units routed to
u32 BaseProcessingUnit::routeToConnected([[maybe_unused]] const void* data, 
                                         [[maybe_unused]] usize size) noexcept {
    // Get connected count
    // count: Origin - Local from atomic load, Scope: function
    const u32 count = connectedCount_.load(std::memory_order_acquire);
    
    // In real implementation, would route to each connected unit
    // For now, just return count
    
    return count;
}

// ==========================================================================
// STATE MANAGEMENT
// ==========================================================================

// Origin: Get current state
// Output: Current state
ProcessingUnitState BaseProcessingUnit::getState() const noexcept {
    return static_cast<ProcessingUnitState>(state_.load(std::memory_order_acquire));
}

// Origin: Transition to new state
// Input: newState - Desired state
// Output: true if successful
bool BaseProcessingUnit::transitionState(ProcessingUnitState newState) noexcept {
    // Get current state
    // currentState: Origin - Local from atomic load, Scope: function
    ProcessingUnitState currentState = getState();
    
    // Validate transition - implement state machine logic
    // Valid transitions:
    // UNINITIALIZED -> INITIALIZING
    // INITIALIZING -> READY, ERROR
    // READY -> PROCESSING, PAUSED, SHUTTING_DOWN
    // PROCESSING -> READY, PAUSED, ERROR, SHUTTING_DOWN
    // PAUSED -> READY, SHUTTING_DOWN
    // ERROR -> SHUTTING_DOWN
    // SHUTTING_DOWN -> TERMINATED
    // TERMINATED -> nowhere
    
    bool validTransition = false;
    switch (currentState) {
        case ProcessingUnitState::UNINITIALIZED:
            validTransition = (newState == ProcessingUnitState::INITIALIZING);
            break;
        case ProcessingUnitState::INITIALIZING:
            validTransition = (newState == ProcessingUnitState::READY || 
                             newState == ProcessingUnitState::ERROR);
            break;
        case ProcessingUnitState::READY:
            validTransition = (newState == ProcessingUnitState::PROCESSING ||
                             newState == ProcessingUnitState::PAUSED ||
                             newState == ProcessingUnitState::SHUTTING_DOWN);
            break;
        case ProcessingUnitState::PROCESSING:
            validTransition = (newState == ProcessingUnitState::READY ||
                             newState == ProcessingUnitState::PAUSED ||
                             newState == ProcessingUnitState::ERROR ||
                             newState == ProcessingUnitState::SHUTTING_DOWN);
            break;
        case ProcessingUnitState::PAUSED:
            validTransition = (newState == ProcessingUnitState::READY ||
                             newState == ProcessingUnitState::SHUTTING_DOWN);
            break;
        case ProcessingUnitState::ERROR:
            validTransition = (newState == ProcessingUnitState::SHUTTING_DOWN);
            break;
        case ProcessingUnitState::SHUTTING_DOWN:
            validTransition = (newState == ProcessingUnitState::TERMINATED);
            break;
        case ProcessingUnitState::TERMINATED:
            validTransition = false; // Cannot transition from TERMINATED
            break;
    }
    
    if (!validTransition) {
        return false;
    }
    
    // expectedValue: Origin - Local for CAS operation, Scope: function
    u8 expectedValue = static_cast<u8>(currentState);
    // newValue: Origin - Local for CAS operation, Scope: function
    u8 newValue = static_cast<u8>(newState);
    
    // Atomic transition
    return state_.compare_exchange_strong(expectedValue, newValue,
                                         std::memory_order_release,
                                         std::memory_order_acquire);
}

// ==========================================================================
// METRICS METHODS
// ==========================================================================

// Origin: Get metrics
// Output: Current metrics
ProcessingUnitMetrics BaseProcessingUnit::getMetrics() const noexcept {
    // Update timestamp
    // now: Origin - Local from clock, Scope: function
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    metrics_.lastUpdateTimestamp.store(now, std::memory_order_relaxed);
    
    // Return copy - copy constructor handles atomic copying
    return metrics_;
}

// Origin: Reset metrics
void BaseProcessingUnit::resetMetrics() noexcept {
    metrics_.ticksProcessed.store(0, std::memory_order_relaxed);
    metrics_.batchesProcessed.store(0, std::memory_order_relaxed);
    metrics_.bytesProcessed.store(0, std::memory_order_relaxed);
    metrics_.totalProcessingTimeNs.store(0, std::memory_order_relaxed);
    metrics_.minLatencyNs.store(UINT64_MAX, std::memory_order_relaxed);
    metrics_.maxLatencyNs.store(0, std::memory_order_relaxed);
    metrics_.errorCount.store(0, std::memory_order_relaxed);
    metrics_.skipCount.store(0, std::memory_order_relaxed);
    metrics_.queueDepth.store(0, std::memory_order_relaxed);
    // Don't reset connectedUnits - that's configuration
    metrics_.lastUpdateTimestamp.store(0, std::memory_order_relaxed);
}

// Origin: Update metrics after processing
// Input: startTime - Start timestamp
//        itemsProcessed - Items processed
//        bytesProcessed - Bytes processed
void BaseProcessingUnit::updateMetrics(u64 startTime, u32 itemsProcessed, 
                                       u64 bytesProcessed) noexcept {
    // Calculate latency
    // endTime: Origin - Local from clock, Scope: function
    auto endTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    // latency: Origin - Local calculation, Scope: function
    u64 latency = endTime - startTime;
    
    // Update counters
    metrics_.ticksProcessed.fetch_add(itemsProcessed, std::memory_order_relaxed);
    metrics_.bytesProcessed.fetch_add(bytesProcessed, std::memory_order_relaxed);
    metrics_.totalProcessingTimeNs.fetch_add(latency, std::memory_order_relaxed);
    
    // Update min latency
    // currentMin: Origin - Local from atomic load, Scope: loop
    u64 currentMin = metrics_.minLatencyNs.load(std::memory_order_relaxed);
    while (latency < currentMin) {
        if (metrics_.minLatencyNs.compare_exchange_weak(currentMin, latency,
                                                        std::memory_order_relaxed)) {
            break;
        }
    }
    
    // Update max latency
    // currentMax: Origin - Local from atomic load, Scope: loop
    u64 currentMax = metrics_.maxLatencyNs.load(std::memory_order_relaxed);
    while (latency > currentMax) {
        if (metrics_.maxLatencyNs.compare_exchange_weak(currentMax, latency,
                                                        std::memory_order_relaxed)) {
            break;
        }
    }
}

// ==========================================================================
// CONFIGURATION METHODS
// ==========================================================================

// Origin: Validate configuration
// Input: config - Configuration to validate
// Output: true if valid
bool BaseProcessingUnit::validateConfig(const ProcessingUnitConfig& config) const noexcept {
    // Check unit ID is valid
    if (config.unitId == 0) {  // ProcessingUnitId is u64, not struct
        return false;
    }
    
    // Check buffer sizes are power of 2
    if ((config.inputBufferSize & (config.inputBufferSize - 1)) != 0) {
        return false;
    }
    if ((config.outputBufferSize & (config.outputBufferSize - 1)) != 0) {
        return false;
    }
    
    // Check NUMA node is valid
    if (config.numaNode >= static_cast<i32>(MAX_NUMA_NODES)) {
        return false;
    }
    
    // Check latency is reasonable
    if (config.maxLatencyNs == 0 || config.maxLatencyNs > 1'000'000'000) {  // 1 second max
        return false;
    }
    
    return true;
}

// Origin: Reconfigure at runtime
// Input: config - New configuration
// Output: ResultCode
ResultCode BaseProcessingUnit::reconfigure(const ProcessingUnitConfig& config) noexcept {
    // Only allow reconfiguration in READY or PAUSED state
    // currentState: Origin - Local from atomic load, Scope: function
    ProcessingUnitState currentState = getState();
    
    if (currentState != ProcessingUnitState::READY &&
        currentState != ProcessingUnitState::PAUSED) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    // Validate new configuration
    if (!validateConfig(config)) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    // Apply configuration
    config_ = config;
    
    return ResultCode::SUCCESS;
}

// ==========================================================================
// ENFORCE EXTREME PRINCIPLES
// ==========================================================================

ENFORCE_EXTREME_PRINCIPLES();

} // namespace AARendoCoreGLM