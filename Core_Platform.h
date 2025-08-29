// Core_Platform.h - FOUNDATION LAYER - COMPILER PROCESSES FIRST
// NO DEPENDENCIES - This is the absolute beginning
// Every decision here affects EVERYTHING that follows

#ifndef AARENDOCOREGLM_CORE_PLATFORM_H
#define AARENDOCOREGLM_CORE_PLATFORM_H

// ============================================================================
// PLATFORM DETECTION - The compiler's first decisions
// ============================================================================

// Operating System Detection
#ifdef _WIN32
    #define AARENDOCORE_PLATFORM_WINDOWS 1
    #ifdef _WIN64
        #define AARENDOCORE_PLATFORM_WIN64 1
        #define AARENDOCORE_PLATFORM_WIN32 0
    #else
        #define AARENDOCORE_PLATFORM_WIN64 0
        #define AARENDOCORE_PLATFORM_WIN32 1
    #endif
#else
    #define AARENDOCORE_PLATFORM_WINDOWS 0
    #define AARENDOCORE_PLATFORM_WIN64 0
    #define AARENDOCORE_PLATFORM_WIN32 0
#endif

// Linux Detection (for WSL)
#ifdef __linux__
    #define AARENDOCORE_PLATFORM_LINUX 1
#else
    #define AARENDOCORE_PLATFORM_LINUX 0
#endif

// ============================================================================
// COMPILER DETECTION - Know thy compiler
// ============================================================================

#ifdef _MSC_VER
    #define AARENDOCORE_COMPILER_MSVC 1
    #define AARENDOCORE_COMPILER_VERSION _MSC_VER
    
    // MSVC specific settings
    #pragma warning(push, 4)  // Maximum warning level
    #pragma warning(disable: 4514)  // Unreferenced inline function removed
    #pragma warning(disable: 4820)  // Padding added
#else
    #define AARENDOCORE_COMPILER_MSVC 0
#endif

#ifdef __GNUC__
    #define AARENDOCORE_COMPILER_GCC 1
    #define AARENDOCORE_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100)
#else
    #define AARENDOCORE_COMPILER_GCC 0
#endif

// ============================================================================
// CPU ARCHITECTURE - The hardware we're targeting
// ============================================================================

#if defined(_M_X64) || defined(__x86_64__)
    #define AARENDOCORE_ARCH_X64 1
    #define AARENDOCORE_ARCH_X86 0
#elif defined(_M_IX86) || defined(__i386__)
    #define AARENDOCORE_ARCH_X64 0
    #define AARENDOCORE_ARCH_X86 1
#else
    #error "Unsupported CPU architecture"
#endif

// ============================================================================
// SIMD SUPPORT DETECTION - For our AVX2 operations
// ============================================================================

#ifdef __AVX2__
    #define AARENDOCORE_HAS_AVX2 1
#else
    #define AARENDOCORE_HAS_AVX2 0
#endif

#ifdef __AVX__
    #define AARENDOCORE_HAS_AVX 1
#else
    #define AARENDOCORE_HAS_AVX 0
#endif

#ifdef __SSE4_2__
    #define AARENDOCORE_HAS_SSE42 1
#else
    #define AARENDOCORE_HAS_SSE42 0
#endif

// ============================================================================
// CRITICAL SYSTEM CONSTANTS - The foundation of all memory operations
// ============================================================================

// Cache line size - CRITICAL for performance
#define AARENDOCORE_CACHE_LINE_SIZE 64

// Page size - for NUMA operations
#define AARENDOCORE_PAGE_SIZE 4096

// NUMA page size - ultra-extreme alignment
#define AARENDOCORE_NUMA_PAGE_SIZE 2097152  // 2MB huge pages

// Ultra page size - our psychotic alignment requirement
#define AARENDOCORE_ULTRA_PAGE_SIZE 2048

// ============================================================================
// COMPILER ATTRIBUTES - Tell the compiler exactly what we want
// ============================================================================

#if AARENDOCORE_COMPILER_MSVC
    #define AARENDOCORE_FORCEINLINE __forceinline
    #define AARENDOCORE_NOINLINE __declspec(noinline)
    #define AARENDOCORE_ALIGNED(x) __declspec(align(x))
    #define AARENDOCORE_RESTRICT __restrict
    #define AARENDOCORE_NORETURN __declspec(noreturn)
    #define AARENDOCORE_LIKELY(x) (x)
    #define AARENDOCORE_UNLIKELY(x) (x)
#elif AARENDOCORE_COMPILER_GCC
    #define AARENDOCORE_FORCEINLINE __attribute__((always_inline)) inline
    #define AARENDOCORE_NOINLINE __attribute__((noinline))
    #define AARENDOCORE_ALIGNED(x) __attribute__((aligned(x)))
    #define AARENDOCORE_RESTRICT __restrict__
    #define AARENDOCORE_NORETURN __attribute__((noreturn))
    #define AARENDOCORE_LIKELY(x) __builtin_expect(!!(x), 1)
    #define AARENDOCORE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define AARENDOCORE_FORCEINLINE inline
    #define AARENDOCORE_NOINLINE
    #define AARENDOCORE_ALIGNED(x)
    #define AARENDOCORE_RESTRICT
    #define AARENDOCORE_NORETURN
    #define AARENDOCORE_LIKELY(x) (x)
    #define AARENDOCORE_UNLIKELY(x) (x)
#endif

// ============================================================================
// DLL EXPORT/IMPORT - For our DLL boundary
// ============================================================================

#if AARENDOCORE_PLATFORM_WINDOWS
    #ifdef AARENDOCORE_EXPORTS
        #define AARENDOCORE_API __declspec(dllexport)
    #else
        #define AARENDOCORE_API __declspec(dllimport)
    #endif
#else
    #define AARENDOCORE_API
#endif


// ============================================================================
// DEBUG/RELEASE DETECTION
// ============================================================================

#ifdef NDEBUG
    #define AARENDOCORE_DEBUG 0
    #define AARENDOCORE_RELEASE 1
#else
    #define AARENDOCORE_DEBUG 1
    #define AARENDOCORE_RELEASE 0
#endif

// ============================================================================
// ASSERTION MACROS - For our psychotic validation
// ============================================================================

#if AARENDOCORE_DEBUG
    #include <cassert>
    #define AARENDOCORE_ASSERT(x) assert(x)
    #define AARENDOCORE_VERIFY(x) assert(x)
#else
    #define AARENDOCORE_ASSERT(x) ((void)0)
    #define AARENDOCORE_VERIFY(x) ((void)(x))
#endif

// ============================================================================
// NAMESPACE - Everything lives here
// ============================================================================

#define AARENDOCORE_NAMESPACE_BEGIN namespace AARendoCoreGLM {
#define AARENDOCORE_NAMESPACE_END }

// ============================================================================
// PLATFORM VALIDATION - Ensure we're on a supported platform
// ============================================================================

#if !AARENDOCORE_PLATFORM_WINDOWS && !AARENDOCORE_PLATFORM_LINUX
    #error "AARendoCoreGLM requires Windows or Linux (WSL)"
#endif

#if !AARENDOCORE_ARCH_X64
    #error "AARendoCoreGLM requires x64 architecture"
#endif

#if !AARENDOCORE_HAS_AVX2 && AARENDOCORE_RELEASE
    #warning "AARendoCoreGLM performs best with AVX2 support"
#endif

#endif // AARENDOCOREGLM_CORE_PLATFORM_H