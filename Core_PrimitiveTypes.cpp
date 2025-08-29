//===--- Core_PrimitiveTypes.cpp - Primitive Type Implementations --------===//
//
// COMPILATION LEVEL: 0 (ZERO DEPENDENCIES)
// ORIGIN: Implementation file for Core_PrimitiveTypes.h
// DEPENDENCIES: Core_PrimitiveTypes.h only
// DEPENDENTS: None
//
// This file contains validation and utility functions for primitive types.
//===----------------------------------------------------------------------===//

#include "Core_PrimitiveTypes.h"

namespace AARendoCore {

// ==========================================================================
// TYPE VALIDATION FUNCTIONS
// ==========================================================================

// Origin: Function to validate ResultCode
// Input: code (ResultCode to validate)
// Output: true if valid, false otherwise
// Scope: Function duration
constexpr bool IsValidResultCode(ResultCode code) noexcept {
    // currentValue: Origin - Local variable from parameter cast, Scope: function duration
    const u32 currentValue = static_cast<u32>(code);
    
    // maxValue: Origin - Local constant, Scope: function duration  
    constexpr u32 maxValue = static_cast<u32>(ResultCode::ERROR_LOCK_DETECTED);
    
    return currentValue <= maxValue;
}

// Origin: Function to validate ProcessResult
// Input: result (ProcessResult to validate)
// Output: true if valid, false otherwise
// Scope: Function duration
constexpr bool IsValidProcessResult(ProcessResult result) noexcept {
    // currentValue: Origin - Local variable from parameter cast, Scope: function duration
    const u8 currentValue = static_cast<u8>(result);
    
    // maxValue: Origin - Local constant, Scope: function duration
    constexpr u8 maxValue = static_cast<u8>(ProcessResult::FAILED);
    
    return currentValue <= maxValue;
}

// ==========================================================================
// ID VALIDATION FUNCTIONS
// ==========================================================================

// Origin: Function to validate SessionId
// Input: id (SessionId to validate)
// Output: true if valid (non-zero and within range), false otherwise
// Scope: Function duration
constexpr bool IsValidSessionId(SessionId id) noexcept {
    return id.value > 0 && id.value <= MAX_SESSIONS;
}

// Origin: Function to validate ProcessingUnitId
// Input: id (ProcessingUnitId to validate)
// Output: true if valid (non-zero), false otherwise
// Scope: Function duration
constexpr bool IsValidProcessingUnitId(ProcessingUnitId id) noexcept {
    return id.value > 0;
}

// Origin: Function to validate StreamId
// Input: id (StreamId to validate)
// Output: true if valid (within stream limits), false otherwise
// Scope: Function duration
constexpr bool IsValidStreamId(StreamId id) noexcept {
    return id.value < MAX_INPUT_STREAMS || id.value < MAX_OUTPUT_STREAMS;
}

// Origin: Function to validate NodeId
// Input: id (NodeId to validate)
// Output: true if valid (non-zero), false otherwise
// Scope: Function duration
constexpr bool IsValidNodeId(NodeId id) noexcept {
    return id.value > 0;
}

// ==========================================================================
// STATIC INITIALIZATION VALIDATION
// ==========================================================================

namespace {
    // Origin: Anonymous namespace for compile-time validation
    // These trigger compile-time checks when the file is compiled
    
    // Test ID type safety - these should NOT compile if uncommented:
    // SessionId sid = ProcessingUnitId(1);  // ERROR: No implicit conversion
    // u64 raw = SessionId(1);               // ERROR: No implicit conversion
    
    // Test valid construction
    constexpr SessionId testSession(12345);
    constexpr ProcessingUnitId testUnit(1);
    constexpr StreamId testStream(100);
    
    // Compile-time validation
    static_assert(IsValidSessionId(testSession), "Test session ID must be valid");
    static_assert(IsValidProcessingUnitId(testUnit), "Test unit ID must be valid");
    static_assert(IsValidStreamId(testStream), "Test stream ID must be valid");
    
    // Test result code validation
    static_assert(IsValidResultCode(ResultCode::SUCCESS), "SUCCESS must be valid");
    static_assert(IsValidResultCode(ResultCode::ERROR_LOCK_DETECTED), "Last error must be valid");
    
    // Test process result validation
    static_assert(IsValidProcessResult(ProcessResult::SUCCESS), "SUCCESS must be valid");
    static_assert(IsValidProcessResult(ProcessResult::FAILED), "FAILED must be valid");
}

// ==========================================================================
// RUNTIME VALIDATION (for debug builds)
// ==========================================================================

#ifdef DEBUG
// Origin: Debug-only runtime validation function
// Input: None
// Output: true if all validations pass
// Scope: Function duration
bool ValidatePrimitiveTypes() noexcept {
    // Test atomic operations are lock-free at runtime
    if (!AtomicU64::is_always_lock_free) return false;
    if (!AtomicU32::is_always_lock_free) return false;
    
    // Test size assumptions
    if (sizeof(void*) != sizeof(uptr)) return false;
    if (sizeof(usize) != sizeof(void*)) return false;
    
    // Test alignment assumptions
    if (alignof(std::max_align_t) > CACHE_LINE_SIZE) return false;
    
    // Test constant relationships
    if (ULTRA_PAGE_SIZE % CACHE_LINE_SIZE != 0) return false;
    if (PAGE_SIZE % CACHE_LINE_SIZE != 0) return false;
    
    return true;
}

// Origin: Static initialization check
// Executed at program startup in debug builds
static const bool primitiveTypesValid = ValidatePrimitiveTypes();
#endif

} // namespace AARendoCore