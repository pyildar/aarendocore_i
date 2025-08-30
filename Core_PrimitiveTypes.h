//===--- Core_PrimitiveTypes.h - Primitive Type Definitions -------------===//
//
// COMPILATION LEVEL: 0 (ZERO DEPENDENCIES)
// ORIGIN: NEW - Base primitive types for entire system
// DEPENDENCIES: NONE (except standard C++ headers)
// DEPENDENTS: ALL other files will depend on this
//
// This file MUST compile first. NO project includes allowed.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_PRIMITIVETYPES_H
#define AARENDOCORE_CORE_PRIMITIVETYPES_H

// STANDARD HEADERS ONLY - NO PROJECT INCLUDES
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <atomic>
#include <limits>

// COMPILER ENFORCEMENT - THIS FILE IS LEVEL 0
#define COMPILING_LEVEL_0
#define CURRENT_COMPILATION_LEVEL 0

// NAMESPACE: AARendoCore - All types in this namespace
namespace AARendoCoreGLM {

// ==========================================================================
// PRIMITIVE TYPE ALIASES - EXACT SIZE ENFORCEMENT
// ==========================================================================

// UNSIGNED INTEGERS - Origin: Standard types, Scope: Global
using u8  = uint8_t;   // 8-bit unsigned, Range: [0, 255]
using u16 = uint16_t;  // 16-bit unsigned, Range: [0, 65535]
using u32 = uint32_t;  // 32-bit unsigned, Range: [0, 4294967295]
using u64 = uint64_t;  // 64-bit unsigned, Range: [0, 18446744073709551615]

// SIGNED INTEGERS - Origin: Standard types, Scope: Global
using i8  = int8_t;    // 8-bit signed, Range: [-128, 127]
using i16 = int16_t;   // 16-bit signed, Range: [-32768, 32767]
using i32 = int32_t;   // 32-bit signed, Range: [-2147483648, 2147483647]
using i64 = int64_t;   // 64-bit signed, Range: [-9223372036854775808, 9223372036854775807]

// FLOATING POINT - Origin: Standard types, Scope: Global
using f32 = float;     // 32-bit float, IEEE 754 single precision
using f64 = double;    // 64-bit float, IEEE 754 double precision

// SIZE TYPES - Origin: Standard types, Scope: Global
using usize = std::size_t;      // Platform size type
using isize = std::ptrdiff_t;   // Platform signed size type

// POINTER TYPES - Origin: Standard types, Scope: Global
using uptr = std::uintptr_t;    // Pointer as integer
using nullptr_t = std::nullptr_t;

// ==========================================================================
// ATOMIC PRIMITIVES - LOCK-FREE GUARANTEED
// ==========================================================================

// ATOMIC TYPES - Origin: std::atomic wrappers, Scope: Global
using AtomicU8  = std::atomic<u8>;
using AtomicU16 = std::atomic<u16>;
using AtomicU32 = std::atomic<u32>;
using AtomicU64 = std::atomic<u64>;

using AtomicI8  = std::atomic<i8>;
using AtomicI16 = std::atomic<i16>;
using AtomicI32 = std::atomic<i32>;
using AtomicI64 = std::atomic<i64>;

using AtomicF32 = std::atomic<f32>;
using AtomicF64 = std::atomic<f64>;

using AtomicBool = std::atomic<bool>;
using AtomicSize = std::atomic<usize>;

// VERIFY LOCK-FREE AT COMPILE TIME
static_assert(std::atomic<u64>::is_always_lock_free, "u64 atomics must be lock-free");
static_assert(std::atomic<u32>::is_always_lock_free, "u32 atomics must be lock-free");

// ==========================================================================
// ID TYPES - STRONGLY TYPED IDS (NO IMPLICIT CONVERSION)
// ==========================================================================

// Template for type-safe IDs
template<typename Tag, typename T = u64>
struct TypedId {
    T value;
    
    // Explicit construction only - no implicit conversions
    explicit constexpr TypedId(T v) noexcept : value(v) {}
    constexpr TypedId() noexcept : value(0) {}
    
    // Comparison operators
    constexpr bool operator==(const TypedId& other) const noexcept { return value == other.value; }
    constexpr bool operator!=(const TypedId& other) const noexcept { return value != other.value; }
    constexpr bool operator<(const TypedId& other) const noexcept { return value < other.value; }
    
    // Hash support
    constexpr u64 hash() const noexcept { return value; }
};

// SPECIFIC ID TYPES - Origin: TypedId template, Scope: Global
struct SessionIdTag {};
struct ProcessingUnitIdTag {};
struct StreamIdTag {};
struct NodeIdTag {};
struct LogicIdTag {};
struct TopicIdTag {};

using SessionId = TypedId<SessionIdTag, u64>;
using ProcessingUnitId = TypedId<ProcessingUnitIdTag, u32>;
using StreamId = TypedId<StreamIdTag, u32>;
using NodeId = TypedId<NodeIdTag, u32>;
using LogicId = TypedId<LogicIdTag, u32>;
using TopicId = TypedId<TopicIdTag, u32>;

// ==========================================================================
// RESULT TYPES - EXPLICIT ERROR HANDLING
// ==========================================================================

// Result codes - Origin: Enumeration, Scope: Global
enum class ResultCode : u32 {
    SUCCESS = 0,
    ERROR_INVALID_PARAMETER = 1,
    ERROR_OUT_OF_MEMORY = 2,
    ERROR_TIMEOUT = 3,
    ERROR_NOT_FOUND = 4,
    ERROR_ALREADY_EXISTS = 5,
    ERROR_CAPACITY_EXCEEDED = 6,
    ERROR_CIRCULAR_DEPENDENCY = 7,
    ERROR_NUMA_FAILURE = 8,
    ERROR_ALIGNMENT_VIOLATION = 9,
    ERROR_LOCK_DETECTED = 10  // Should never happen with our design
};

// Process result - Origin: Enumeration, Scope: Global
enum class ProcessResult : u8 {
    SUCCESS = 0,
    SKIP = 1,
    RETRY = 2,
    FAILED = 3
};

// ==========================================================================
// CONSTANTS - SYSTEM-WIDE LIMITS
// ==========================================================================

// SYSTEM LIMITS - Origin: Requirements, Scope: Global compile-time constants
constexpr u64 MAX_SESSIONS = 10'000'000;  // 10M sessions requirement
constexpr u32 MAX_NUMA_NODES = 8;         // Maximum NUMA nodes
constexpr u32 MAX_INPUT_STREAMS = 1024;   // Maximum input streams
constexpr u32 MAX_OUTPUT_STREAMS = 1024;  // Maximum output streams
constexpr u32 CACHE_LINE_SIZE = 64;       // CPU cache line size
constexpr u32 PAGE_SIZE = 4096;           // Memory page size
constexpr u32 ULTRA_PAGE_SIZE = 2048;     // Our ultra alignment
constexpr u32 QUEUE_SIZE = 65536;         // Lock-free queue size (power of 2)
constexpr u32 BUFFER_SIZE = 16384;        // Stream buffer size

// MEMORY SIZES - Origin: Design requirements, Scope: Global
constexpr u64 KB = 1024;
constexpr u64 MB = 1024 * KB;
constexpr u64 GB = 1024 * MB;
constexpr u64 TB = 1024 * GB;

// TIME CONSTANTS - Origin: Design requirements, Scope: Global
constexpr u64 NANOSECONDS_PER_SECOND = 1'000'000'000;
constexpr u64 MICROSECONDS_PER_SECOND = 1'000'000;
constexpr u64 MILLISECONDS_PER_SECOND = 1'000;

// ==========================================================================
// VALIDATION - COMPILE-TIME CHECKS
// ==========================================================================

// Verify type sizes
static_assert(sizeof(u8) == 1, "u8 must be 1 byte");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");

// Verify alignment requirements
static_assert(alignof(u64) <= 8, "u64 alignment check");
static_assert(CACHE_LINE_SIZE == 64, "Cache line must be 64 bytes");
static_assert((QUEUE_SIZE & (QUEUE_SIZE - 1)) == 0, "Queue size must be power of 2");

// Verify system requirements
static_assert(MAX_SESSIONS == 10'000'000, "Must support exactly 10M sessions");
static_assert(MAX_NUMA_NODES >= 1 && MAX_NUMA_NODES <= 256, "Valid NUMA node range");

} // namespace AARendoCoreGLM

#endif // AARENDOCORE_CORE_PRIMITIVETYPES_H