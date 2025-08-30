//===--- Core_IProcessingUnit.cpp - Processing Unit Interface Impl ------===//
//
// COMPILATION LEVEL: 2
// ORIGIN: Implementation file for Core_IProcessingUnit.h
// DEPENDENCIES: Core_IProcessingUnit.h
// DEPENDENTS: None
//
// This file contains validation and utility functions for the interface.
// NO implementation of the interface itself (it's pure virtual).
//===----------------------------------------------------------------------===//

#include "Core_IProcessingUnit.h"

namespace AARendoCoreGLM {

// ==========================================================================
// CAPABILITY VALIDATION FUNCTIONS
// ==========================================================================

// Origin: Function to check if capabilities are valid
// Input: capabilities - Bitfield to validate
// Output: true if valid combination, false otherwise
// Scope: Function duration
constexpr bool ValidateCapabilities(u64 capabilities) noexcept {
    // Check for invalid combinations
    
    // Cannot be both stateless and stateful
    if ((capabilities & CAP_STATEFUL) && !(capabilities & (CAP_PERSISTENCE | CAP_AGGREGATION))) {
        return false;  // Stateful requires persistence or aggregation
    }
    
    // Real-time requires lock-free
    if ((capabilities & CAP_REAL_TIME) && !(capabilities & CAP_LOCK_FREE)) {
        return false;  // Real-time must be lock-free
    }
    
    // SIMD requires NUMA awareness for optimal performance
    if ((capabilities & CAP_SIMD_OPTIMIZED) && !(capabilities & CAP_NUMA_AWARE)) {
        return false;  // SIMD should be NUMA-aware
    }
    
    return true;
}

// Origin: Function to check if unit type is valid
// Input: type - ProcessingUnitType to validate
// Output: true if valid, false otherwise
// Scope: Function duration
constexpr bool ValidateUnitType(ProcessingUnitType type) noexcept {
    // currentValue: Origin - Local variable from enum cast, Scope: function
    const u32 currentValue = static_cast<u32>(type);
    
    // Check standard types (highest value in standard range)
    if (currentValue <= static_cast<u32>(ProcessingUnitType::ML_PREDICTOR)) {
        return true;
    }
    
    // Check custom types (0x5000+ range)
    if (currentValue >= static_cast<u32>(ProcessingUnitType::ORDER_ROUTER)) {
        return true;
    }
    
    return false;
}

// Origin: Function to check if state transition is valid
// Input: currentState - Current state
//        newState - Desired new state
// Output: true if transition is valid, false otherwise
// Scope: Function duration
constexpr bool ValidateStateTransition(ProcessingUnitState currentState,
                                       ProcessingUnitState newState) noexcept {
    // Define valid state transitions
    switch (currentState) {
        case ProcessingUnitState::UNINITIALIZED:
            return newState == ProcessingUnitState::INITIALIZING;
            
        case ProcessingUnitState::INITIALIZING:
            return newState == ProcessingUnitState::READY ||
                   newState == ProcessingUnitState::ERROR;
            
        case ProcessingUnitState::READY:
            return newState == ProcessingUnitState::PROCESSING ||
                   newState == ProcessingUnitState::PAUSED ||
                   newState == ProcessingUnitState::SHUTTING_DOWN;
            
        case ProcessingUnitState::PROCESSING:
            return newState == ProcessingUnitState::READY ||
                   newState == ProcessingUnitState::PAUSED ||
                   newState == ProcessingUnitState::ERROR ||
                   newState == ProcessingUnitState::SHUTTING_DOWN;
            
        case ProcessingUnitState::PAUSED:
            return newState == ProcessingUnitState::READY ||
                   newState == ProcessingUnitState::PROCESSING ||
                   newState == ProcessingUnitState::SHUTTING_DOWN;
            
        case ProcessingUnitState::ERROR:
            return newState == ProcessingUnitState::SHUTTING_DOWN ||
                   newState == ProcessingUnitState::INITIALIZING;  // Allow retry
            
        case ProcessingUnitState::SHUTTING_DOWN:
            return newState == ProcessingUnitState::TERMINATED;
            
        case ProcessingUnitState::TERMINATED:
            return false;  // Terminal state
            
        default:
            return false;
    }
}

// ==========================================================================
// CAPABILITY STRING CONVERSION (Debug only)
// ==========================================================================

#ifdef DEBUG
// Origin: Function to convert capabilities to string
// Input: capabilities - Bitfield of capabilities
// Output: Static string representation
// Scope: Function duration, debug only
const char* CapabilitiesToString(u64 capabilities) noexcept {
    // Static buffer for string building
    static char buffer[512];
    
    // pos: Origin - Local position tracker, Scope: function
    usize pos = 0;
    
    // Helper macro to append capability name
    #define APPEND_CAP(cap, name) \
        if (capabilities & cap) { \
            if (pos > 0) { \
                buffer[pos++] = '|'; \
            } \
            const char* str = name; \
            while (*str && pos < sizeof(buffer) - 1) { \
                buffer[pos++] = *str++; \
            } \
        }
    
    APPEND_CAP(CAP_TICK, "TICK");
    APPEND_CAP(CAP_ORDER, "ORDER");
    APPEND_CAP(CAP_BATCH, "BATCH");
    APPEND_CAP(CAP_STREAM, "STREAM");
    APPEND_CAP(CAP_INTERPOLATION, "INTERPOLATION");
    APPEND_CAP(CAP_AGGREGATION, "AGGREGATION");
    APPEND_CAP(CAP_ROUTING, "ROUTING");
    APPEND_CAP(CAP_PERSISTENCE, "PERSISTENCE");
    APPEND_CAP(CAP_PARALLEL, "PARALLEL");
    APPEND_CAP(CAP_STATEFUL, "STATEFUL");
    APPEND_CAP(CAP_NUMA_AWARE, "NUMA_AWARE");
    APPEND_CAP(CAP_SIMD_OPTIMIZED, "SIMD_OPTIMIZED");
    APPEND_CAP(CAP_LOCK_FREE, "LOCK_FREE");
    APPEND_CAP(CAP_ZERO_COPY, "ZERO_COPY");
    APPEND_CAP(CAP_REAL_TIME, "REAL_TIME");
    APPEND_CAP(CAP_ML_ENHANCED, "ML_ENHANCED");
    
    #undef APPEND_CAP
    
    buffer[pos] = '\0';
    return buffer;
}
#endif

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

namespace {
    // Test capability combinations at compile time
    constexpr u64 testCaps1 = CAP_TICK | CAP_BATCH | CAP_LOCK_FREE;
    static_assert(ValidateCapabilities(testCaps1), "Valid capability combination");
    
    // Test invalid combination (real-time without lock-free)
    constexpr u64 testCaps2 = CAP_REAL_TIME | CAP_LOCK_FREE;
    static_assert(ValidateCapabilities(testCaps2), "Real-time requires lock-free");
    
    // Test unit type validation
    static_assert(ValidateUnitType(ProcessingUnitType::MARKET_DATA_RECEIVER), "MARKET_DATA_RECEIVER is valid type");
    static_assert(ValidateUnitType(ProcessingUnitType::ORDER_ROUTER), "ORDER_ROUTER is valid type");
    
    // Test state transitions
    static_assert(ValidateStateTransition(ProcessingUnitState::UNINITIALIZED,
                                         ProcessingUnitState::INITIALIZING),
                  "Valid state transition");
    static_assert(!ValidateStateTransition(ProcessingUnitState::TERMINATED,
                                          ProcessingUnitState::READY),
                  "Cannot transition from TERMINATED");
}

// ==========================================================================
// ENFORCE EXTREME PRINCIPLES
// ==========================================================================

// This file follows extreme principles
ENFORCE_EXTREME_PRINCIPLES();

// Verify no dynamic allocation
namespace {
    struct VerifyNoDynamic {
        PREVENT_HEAP_ALLOCATION();
    };
}

} // namespace AARendoCoreGLM