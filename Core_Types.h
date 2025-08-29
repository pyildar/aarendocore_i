// Core_Types.h - FUNDAMENTAL TYPE DEFINITIONS
// COMPILER PROCESSES SECOND - Depends ONLY on Core_Platform.h
// Every type here is deliberate, every size calculated, every alignment planned

#ifndef AARENDOCOREGLM_CORE_TYPES_H
#define AARENDOCOREGLM_CORE_TYPES_H

#include "Core_Platform.h"  // ONLY dependency - the foundation

// Standard includes - MINIMAL and PRECISE
#include <cstdint>          // For fixed-size types
#include <cstddef>          // For size_t, nullptr_t
#include <cstring>          // For memcpy, memset
#include <atomic>           // For atomic types
#include <chrono>           // For time types

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// FUNDAMENTAL TYPE ALIASES - The compiler knows EXACTLY what these are
// ============================================================================

// Unsigned integers - EXACT sizes
using u8  = std::uint8_t;      // 1 byte
using u16 = std::uint16_t;     // 2 bytes  
using u32 = std::uint32_t;     // 4 bytes
using u64 = std::uint64_t;     // 8 bytes

// Signed integers - EXACT sizes
using i8  = std::int8_t;       // 1 byte
using i16 = std::int16_t;      // 2 bytes
using i32 = std::int32_t;      // 4 bytes
using i64 = std::int64_t;      // 8 bytes

// Floating point - IEEE 754 standard
using f32 = float;              // 4 bytes - single precision
using f64 = double;             // 8 bytes - double precision

// Size types
using usize = std::size_t;      // Platform-specific size
using isize = std::ptrdiff_t;   // Platform-specific signed size

// Pointer types
using uptr = std::uintptr_t;    // Integer that can hold a pointer
using iptr = std::intptr_t;     // Signed integer that can hold a pointer

// Boolean type
using b8 = bool;                // At least 1 byte

// Character types
using c8  = char;               // ASCII character
using c16 = char16_t;           // UTF-16 character
using c32 = char32_t;           // UTF-32 character

// Byte type
using byte = std::uint8_t;      // Raw byte

// ============================================================================
// ATOMIC TYPE ALIASES - Lock-free guarantees
// ============================================================================

using AtomicU8  = std::atomic<u8>;
using AtomicU16 = std::atomic<u16>;
using AtomicU32 = std::atomic<u32>;
using AtomicU64 = std::atomic<u64>;

using AtomicI8  = std::atomic<i8>;
using AtomicI16 = std::atomic<i16>;
using AtomicI32 = std::atomic<i32>;
using AtomicI64 = std::atomic<i64>;

using AtomicBool = std::atomic<bool>;
using AtomicSize = std::atomic<usize>;

// Verify lock-free at compile time
static_assert(std::atomic<u64>::is_always_lock_free, "AtomicU64 must be lock-free");
static_assert(std::atomic<i64>::is_always_lock_free, "AtomicI64 must be lock-free");

// ============================================================================
// TIME TYPE ALIASES - For high-frequency timing
// ============================================================================

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::nanoseconds;
using Nanoseconds = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

// ============================================================================
// FIXED-SIZE ARRAYS - Stack-allocated, no heap
// ============================================================================

template<typename T, usize N>
struct FixedArray {
    T data[N];
    
    constexpr usize size() const noexcept { return N; }
    constexpr T& operator[](usize idx) noexcept { return data[idx]; }
    constexpr const T& operator[](usize idx) const noexcept { return data[idx]; }
};

// ============================================================================
// SESSION ID TYPE - Critical for 10M sessions
// ============================================================================

struct SessionId {
    u64 value;
    
    constexpr SessionId() noexcept : value(0) {}
    constexpr explicit SessionId(u64 v) noexcept : value(v) {}
    
    constexpr bool operator==(const SessionId& other) const noexcept {
        return value == other.value;
    }
    
    constexpr bool operator!=(const SessionId& other) const noexcept {
        return value != other.value;
    }
    
    constexpr bool operator<(const SessionId& other) const noexcept {
        return value < other.value;
    }
};

// Session ID generation - PSYCHOTIC PRECISION
inline SessionId GenerateSessionId(u64 uniqueValue) noexcept {
    // Incorporate timestamp and unique value for true uniqueness
    // Upper 32 bits: timestamp component
    // Lower 32 bits: unique counter
    u64 timestamp = static_cast<u64>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    u64 id = (timestamp << 32) | (uniqueValue & 0xFFFFFFFF);
    return SessionId{id};
}

// Check if a session ID is valid (non-zero)
inline bool ValidateSessionId(SessionId id) noexcept {
    return id.value != 0;
}

// ============================================================================
// RESULT TYPE - For error handling without exceptions
// ============================================================================

template<typename T>
struct Result {
    union {
        T value;
        char error[64];  // Fixed-size error message
    };
    bool success;
    
    constexpr Result() noexcept : value{}, success(false) {}
    constexpr explicit Result(const T& v) noexcept : value(v), success(true) {}
    
    constexpr bool isOk() const noexcept { return success; }
    constexpr bool isError() const noexcept { return !success; }
    
    constexpr T& unwrap() noexcept { 
        AARENDOCORE_ASSERT(success);
        return value; 
    }
    
    constexpr const T& unwrap() const noexcept { 
        AARENDOCORE_ASSERT(success);
        return value; 
    }
};

// Specialization for void
template<>
struct Result<void> {
    char error[64];
    bool success;
    
    constexpr Result() noexcept : error{}, success(true) {}
    constexpr bool isOk() const noexcept { return success; }
    constexpr bool isError() const noexcept { return !success; }
};

// ============================================================================
// MEMORY SIZE CONSTANTS - For allocation and alignment
// ============================================================================

constexpr usize CACHE_LINE = AARENDOCORE_CACHE_LINE_SIZE;
constexpr usize PAGE_SIZE = AARENDOCORE_PAGE_SIZE;
constexpr usize NUMA_PAGE = AARENDOCORE_NUMA_PAGE_SIZE;
constexpr usize ULTRA_PAGE = AARENDOCORE_ULTRA_PAGE_SIZE;

// Size units
constexpr usize KB = 1024;
constexpr usize MB = 1024 * KB;
constexpr usize GB = 1024 * MB;

// ============================================================================
// HANDLE TYPES - Opaque handles for resources
// ============================================================================

struct MemoryHandle {
    void* ptr;
    usize size;
    u32 numaNode;
    u32 flags;
};

struct ThreadHandle {
    uptr handle;
    u32 affinity;
    u32 priority;
};

// ============================================================================
// ENUM CLASS DEFINITIONS - Type-safe enums
// ============================================================================

enum class StatusCode : u32 {
    Success = 0,
    InvalidArgument = 1,
    OutOfMemory = 2,
    NotInitialized = 3,
    AlreadyExists = 4,
    NotFound = 5,
    Timeout = 6,
    Overflow = 7,
    Underflow = 8,
    SystemError = 9
};

enum class ProcessingMode : u32 {
    RealTime = 0,     // Ultra-low latency
    Batch = 1,        // Batch processing
    Hybrid = 2        // Adaptive mode
};

// ============================================================================
// TYPE TRAITS - Compiler optimizations
// ============================================================================

// Check if type is trivially copyable (can use memcpy)
template<typename T>
constexpr bool IsTrivial = std::is_trivially_copyable_v<T>;

// Check if type is power of 2
template<usize N>
constexpr bool IsPowerOfTwo = (N > 0) && ((N & (N - 1)) == 0);

// ============================================================================
// STATIC ASSERTIONS - Validate type sizes at compile time
// ============================================================================

// Verify fundamental type sizes
static_assert(sizeof(u8)  == 1, "u8 must be 1 byte");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");

static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");

// Verify SessionId is efficient
static_assert(sizeof(SessionId) == 8, "SessionId must be 8 bytes");
static_assert(IsTrivial<SessionId>, "SessionId must be trivially copyable");

// Verify alignment constants are powers of 2
static_assert(IsPowerOfTwo<CACHE_LINE>, "Cache line must be power of 2");
static_assert(IsPowerOfTwo<PAGE_SIZE>, "Page size must be power of 2");
static_assert(IsPowerOfTwo<ULTRA_PAGE>, "Ultra page must be power of 2");

AARENDOCORE_NAMESPACE_END

// ============================================================================
// TYPE SYSTEM EXPORTS - PSYCHOTIC PRECISION
// ============================================================================

extern "C" {
    AARENDOCORE_API const char* AARendoCore_GetTypeInfo();
    AARENDOCORE_API uint64_t AARendoCore_GenerateSessionId();  // PSYCHOTIC PRECISION: Use uint64_t not u64 in extern C
    AARENDOCORE_API bool AARendoCore_ValidateSessionId(uint64_t id);  // PSYCHOTIC PRECISION: Use uint64_t not u64 in extern C
}

#endif // AARENDOCOREGLM_CORE_TYPES_H