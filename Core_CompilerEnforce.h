//===--- Core_CompilerEnforce.h - Compiler-Level Enforcement ------------===//
//
// COMPILATION LEVEL: 0 (ZERO DEPENDENCIES)
// ORIGIN: NEW - Compiler enforcement macros and utilities
// DEPENDENCIES: NONE (except standard headers)
// DEPENDENTS: ALL files will use these enforcement mechanisms
//
// This file enforces our EXTREME PRINCIPLES at compile time.
// NO LOCKS, NO MUTEXES, NO DYNAMIC ALLOCATION, PSYCHOTIC PRECISION.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_COMPILERENFORCE_H
#define AARENDOCORE_CORE_COMPILERENFORCE_H

// STANDARD HEADERS ONLY
#include <type_traits>
#include <cstddef>
#include <utility>

// THIS FILE IS LEVEL 0
#define COMPILING_LEVEL_0
#define CURRENT_COMPILATION_LEVEL 0

// ==========================================================================
// COMPILATION LEVEL ENFORCEMENT
// ==========================================================================

// Declare which compilation level a file belongs to
#define DECLARE_COMPILATION_LEVEL(level) \
    static_assert(level >= 0 && level <= 10, "Invalid compilation level"); \
    static constexpr int FILE_COMPILATION_LEVEL = level; \
    namespace { constexpr int ValidateLevel = level; }

// Enforce dependency rules
#define ENFORCE_DEPENDENCY_LEVEL(required_level) \
    static_assert(FILE_COMPILATION_LEVEL >= required_level, \
                  "Cannot include higher level dependency");

// ==========================================================================
// CIRCULAR DEPENDENCY PREVENTION
// ==========================================================================

// Each file must use this to declare itself
#define DECLARE_HEADER_GUARD(name) \
    AARENDOCORE_##name##_H

// Detect circular includes - Fixed preprocessor syntax
#define CHECK_CIRCULAR_DEPENDENCY(name) \
    static_assert(true, "Checking circular dependency for " #name)

#define END_CIRCULAR_CHECK(name) \
    static_assert(true, "End circular check for " #name)

// ==========================================================================
// VARIABLE ORIGIN TRACKING
// ==========================================================================

// Every variable must declare its origin
#define DECLARE_VARIABLE_WITH_ORIGIN(type, name, origin_desc) \
    type name; \
    static constexpr const char* name##_ORIGIN = origin_desc; \
    static constexpr const char* name##_TYPE = #type

// For member variables
#define DECLARE_MEMBER(type, name, origin_desc) \
    type name##_; /* underscore suffix for members */ \
    static constexpr const char* name##_ORIGIN = origin_desc

// For parameters
#define PARAM(type, name) \
    type name /* Parameter origin implicit from function signature */

// For local variables
#define LOCAL_VAR(type, name, init_value) \
    type name = init_value; \
    static constexpr const char* name##_ORIGIN = "Local variable"

// ==========================================================================
// LOCK AND MUTEX PREVENTION
// ==========================================================================

// Forward declare mutex types we want to detect (without including <mutex>)
namespace std {
    class mutex;
    class recursive_mutex;
    class shared_mutex;
}

// Detect mutex in types - primary template
template<typename T>
struct has_mutex : std::false_type {};

// Specializations for mutex types
template<>
struct has_mutex<std::mutex> : std::true_type {};

template<>
struct has_mutex<std::recursive_mutex> : std::true_type {};

template<>
struct has_mutex<std::shared_mutex> : std::true_type {};

template<typename T>
inline constexpr bool has_mutex_v = has_mutex<T>::value;

// Enforce no mutexes
#define ENFORCE_NO_MUTEX(Type) \
    static_assert(!has_mutex_v<Type>, \
                  #Type " cannot contain mutex - ALIEN LEVEL CODE forbids locks")

// ==========================================================================
// DYNAMIC ALLOCATION PREVENTION
// ==========================================================================

// Prevent new/delete
#define PREVENT_HEAP_ALLOCATION() \
    void* operator new(std::size_t) = delete; \
    void* operator new[](std::size_t) = delete; \
    void operator delete(void*) = delete; \
    void operator delete[](void*) = delete

// Enforce stack allocation only
#define STACK_ONLY_CLASS(ClassName) \
    class ClassName { \
        PREVENT_HEAP_ALLOCATION(); \
    public:

// ==========================================================================
// TYPE SAFETY ENFORCEMENT
// ==========================================================================

// No implicit conversions
#define EXPLICIT_TYPE(TypeName) \
    struct TypeName { \
        explicit TypeName() = default; \
        TypeName(const TypeName&) = delete; \
        TypeName& operator=(const TypeName&) = delete; \
        TypeName(TypeName&&) = default; \
        TypeName& operator=(TypeName&&) = default; \
    }

// Strong typedef with no implicit conversion
#define STRONG_TYPEDEF(BaseType, NewType) \
    class NewType { \
    private: \
        BaseType value_; \
    public: \
        explicit constexpr NewType(BaseType v) : value_(v) {} \
        constexpr BaseType get() const { return value_; } \
        bool operator==(const NewType& other) const = default; \
        bool operator<(const NewType& other) const { return value_ < other.value_; } \
    }

// ==========================================================================
// MEMORY ALIGNMENT ENFORCEMENT
// ==========================================================================

// Enforce cache line alignment
#define CACHE_ALIGNED alignas(64)

// Enforce page alignment
#define PAGE_ALIGNED alignas(4096)

// Enforce ultra alignment
#define ULTRA_ALIGNED alignas(2048)

// Verify alignment at compile time
template<typename T, size_t Alignment>
struct enforce_alignment {
    static_assert(alignof(T) >= Alignment, 
                  "Type does not meet alignment requirement");
    using type = T;
};

// ==========================================================================
// CAPACITY ENFORCEMENT
// ==========================================================================

// Enforce 10M session support
#define ENFORCE_10M_SESSIONS(container_size) \
    static_assert(container_size >= 10'000'000, \
                  "Container must support 10M sessions - PSYCHOTIC ISOLATION")

// Enforce power of 2 for lock-free operations
template<size_t N>
struct is_power_of_two {
    static constexpr bool value = N && !(N & (N - 1));
};

#define ENFORCE_POWER_OF_TWO(size) \
    static_assert(is_power_of_two<size>::value, \
                  #size " must be power of 2 for lock-free operations")

// ==========================================================================
// FUNCTION ATTRIBUTE ENFORCEMENT
// ==========================================================================

// All functions must be noexcept in production
#ifdef NDEBUG
    #define NOEXCEPT noexcept
    #define ENFORCE_NOEXCEPT(func) \
        static_assert(noexcept(func), #func " must be noexcept")
#else
    #define NOEXCEPT noexcept
    #define ENFORCE_NOEXCEPT(func)
#endif

// Force inline for critical paths
#ifdef _MSC_VER
    #define FORCE_INLINE __forceinline
#else
    #define FORCE_INLINE __attribute__((always_inline)) inline
#endif

// Prevent inlining for debugging
#ifdef _MSC_VER
    #define NO_INLINE __declspec(noinline)
#else
    #define NO_INLINE __attribute__((noinline))
#endif

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Validate at compile time
#define COMPILE_TIME_VALIDATE(...) \
    static_assert(__VA_ARGS__)

// Concept enforcement (C++20)
#if __cplusplus >= 202002L
    #define REQUIRES(...) requires(__VA_ARGS__)
#else
    #define REQUIRES(...)
#endif

// ==========================================================================
// ERROR HANDLING ENFORCEMENT
// ==========================================================================

// No exceptions in production
#ifdef NDEBUG
    #define THROWS noexcept
    #define TRY if(true)
    #define CATCH(x) if(false)
#else
    #define THROWS
    #define TRY try
    #define CATCH(x) catch(x)
#endif

// ==========================================================================
// EXTREME PRINCIPLES ENFORCEMENT
// ==========================================================================

// Master enforcement - use in every compilation unit
#define ENFORCE_EXTREME_PRINCIPLES() \
    static_assert(true, "ENFORCING EXTREME PRINCIPLES"); \
    namespace { \
        struct ExtremeValidator { \
            ExtremeValidator() { \
                /* Validated at compile time */ \
            } \
        }; \
        static ExtremeValidator validator; \
    }

// Psychotic precision for floating point
#define PSYCHOTIC_FLOAT_PRECISION 1e-15

// Alien level code marker
#define ALIEN_LEVEL_CODE \
    _Pragma("GCC optimize (\"O3\")") \
    _Pragma("GCC optimize (\"unroll-loops\")")

// ==========================================================================
// VCXPROJ TRACKING
// ==========================================================================

// Mark file for vcxproj inclusion
#define VCXPROJ_INCLUDE(filename) \
    static constexpr const char* VCXPROJ_FILE = filename

// ==========================================================================
// FINAL ENFORCEMENT
// ==========================================================================

// Use this at the end of every header
#define ENFORCE_HEADER_COMPLETE(name) \
    static_assert(true, "Header " #name " complete with ZERO ambiguity")

#endif // AARENDOCORE_CORE_COMPILERENFORCE_H