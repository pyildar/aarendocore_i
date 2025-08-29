// Core_Platform.cpp - FOUNDATION LAYER IMPLEMENTATION
// This file exists to ensure the header compiles correctly
// and to provide any platform-specific runtime checks

#include "Core_Platform.h"

// ============================================================================
// PLATFORM VALIDATION AT RUNTIME
// ============================================================================

AARENDOCORE_NAMESPACE_BEGIN

// Static validation that runs at program startup
namespace {
    struct PlatformValidator {
        PlatformValidator() {
            // Verify cache line size at runtime
            static_assert(AARENDOCORE_CACHE_LINE_SIZE == 64, 
                "Cache line size must be 64 bytes");
            
            // Verify page alignment
            static_assert((AARENDOCORE_PAGE_SIZE & (AARENDOCORE_PAGE_SIZE - 1)) == 0,
                "Page size must be power of 2");
            
            // Verify ultra page alignment
            static_assert((AARENDOCORE_ULTRA_PAGE_SIZE & (AARENDOCORE_ULTRA_PAGE_SIZE - 1)) == 0,
                "Ultra page size must be power of 2");
            
            // Verify we're on a supported platform
            static_assert(AARENDOCORE_PLATFORM_WINDOWS || AARENDOCORE_PLATFORM_LINUX,
                "Unsupported platform");
            
            // Verify we're on x64
            static_assert(AARENDOCORE_ARCH_X64,
                "AARendoCoreGLM requires x64 architecture");
        }
    };
    
    // This will run at program startup
    static PlatformValidator validator;
}

// Export a function to verify platform at runtime
extern "C" AARENDOCORE_API bool AARendoCore_ValidatePlatform() {
    // Runtime checks that can't be done at compile time
    
#if AARENDOCORE_PLATFORM_WINDOWS
    // On Windows platform
    return true;
#elif AARENDOCORE_PLATFORM_LINUX
    // On Linux platform (WSL)
    return true;
#else
    // Unsupported platform
    return false;
#endif
}

// Export platform information
extern "C" AARENDOCORE_API const char* AARendoCore_GetPlatformInfo() {
    static const char* info = 
        "AARendoCoreGLM Platform: "
#if AARENDOCORE_PLATFORM_WIN64
        "Windows 64-bit"
#elif AARENDOCORE_PLATFORM_WIN32
        "Windows 32-bit"
#elif AARENDOCORE_PLATFORM_LINUX
        "Linux"
#else
        "Unknown"
#endif
        " | Compiler: "
#if AARENDOCORE_COMPILER_MSVC
        "MSVC"
#elif AARENDOCORE_COMPILER_GCC
        "GCC"
#else
        "Unknown"
#endif
        " | Architecture: "
#if AARENDOCORE_ARCH_X64
        "x64"
#elif AARENDOCORE_ARCH_X86
        "x86"
#else
        "Unknown"
#endif
        " | SIMD: "
#if AARENDOCORE_HAS_AVX2
        "AVX2"
#elif AARENDOCORE_HAS_AVX
        "AVX"
#elif AARENDOCORE_HAS_SSE42
        "SSE4.2"
#else
        "None"
#endif
        " | Build: "
#if AARENDOCORE_DEBUG
        "Debug"
#else
        "Release"
#endif
    ;
    
    return info;
}

AARENDOCORE_NAMESPACE_END