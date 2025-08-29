// Core_Alignment.cpp - ALIGNMENT VALIDATION AND UTILITIES
// Validates alignment properties and provides alignment utilities

#include "Core_Alignment.h"
#include <cstdio>
#include <new>  // For aligned allocation

#if AARENDOCORE_PLATFORM_WINDOWS
    #include <malloc.h>  // For _aligned_malloc, _aligned_free
#else
    #include <cstdlib>   // For posix_memalign, free
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// ALIGNMENT VALIDATION
// ============================================================================

namespace {
    // Validate alignment properties at startup
    struct AlignmentValidator {
        AlignmentValidator() {
            ValidateAlignmentSizes();
            ValidateAlignedStructures();
            ValidateSimdAlignment();
        }
        
    private:
        void ValidateAlignmentSizes() {
            // Verify all alignment values
            AARENDOCORE_ASSERT(CACHE_LINE == 64);
            AARENDOCORE_ASSERT(PAGE_SIZE == 4096);
            AARENDOCORE_ASSERT(NUMA_PAGE == 2097152);
            AARENDOCORE_ASSERT(ULTRA_PAGE == 2048);
            AARENDOCORE_ASSERT(SIMD_ALIGNMENT == 32);
            
            // Verify they're all powers of 2
            AARENDOCORE_ASSERT((CACHE_LINE & (CACHE_LINE - 1)) == 0);
            AARENDOCORE_ASSERT((PAGE_SIZE & (PAGE_SIZE - 1)) == 0);
            AARENDOCORE_ASSERT((NUMA_PAGE & (NUMA_PAGE - 1)) == 0);
            AARENDOCORE_ASSERT((ULTRA_PAGE & (ULTRA_PAGE - 1)) == 0);
            AARENDOCORE_ASSERT((SIMD_ALIGNMENT & (SIMD_ALIGNMENT - 1)) == 0);
        }
        
        void ValidateAlignedStructures() {
            // Test CacheAlignedValue
            CacheAlignedValue<u64> cav1(100);
            CacheAlignedValue<u64> cav2(200);
            
            // They should be on different cache lines
            auto addr1 = reinterpret_cast<uptr>(&cav1);
            auto addr2 = reinterpret_cast<uptr>(&cav2);
            
            // Verify addresses are on different cache lines
            AARENDOCORE_ASSERT((addr2 - addr1) >= CACHE_LINE);
            
            // Verify alignment
            AARENDOCORE_ASSERT(IsAligned(&cav1, CACHE_LINE));
            AARENDOCORE_ASSERT(IsAligned(&cav2, CACHE_LINE));
            
            // Test CacheAlignedAtomic
            CacheAlignedAtomic<u64> caa(42);
            AARENDOCORE_ASSERT(IsAligned(&caa, CACHE_LINE));
            AARENDOCORE_ASSERT(caa.load() == 42);
            caa.store(100);
            AARENDOCORE_ASSERT(caa.load() == 100);
            
            // Test UltraAlignedData
            UltraAlignedData<u64> uad(999);
            AARENDOCORE_ASSERT(IsAligned(&uad, ULTRA_PAGE));
            AARENDOCORE_ASSERT(*uad == 999);
        }
        
        void ValidateSimdAlignment() {
            // Test SIMD vectors
            SimdFloat8 sf8;
            SimdDouble4 sd4;
            
            AARENDOCORE_ASSERT(IsAligned(&sf8, SIMD_ALIGNMENT));
            AARENDOCORE_ASSERT(IsAligned(&sd4, SIMD_ALIGNMENT));
            
            // Test sizes
            AARENDOCORE_ASSERT(sizeof(sf8) == 32);  // 8 floats * 4 bytes
            AARENDOCORE_ASSERT(sizeof(sd4) == 32);  // 4 doubles * 8 bytes
        }
    };
    
    // Run validation at startup
    [[maybe_unused]] static AlignmentValidator validator;
}

// ============================================================================
// ALIGNMENT INFORMATION EXPORTS
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetAlignmentInfo() {
    static char info[512];
    
    std::snprintf(info, sizeof(info),
        "Alignments: CacheLine=%zu, Page=%zu, NumaPage=%zu, UltraPage=%zu, SIMD=%zu | "
        "Sizes: CacheAlignedValue=%zu, UltraAlignedData=%zu, SimdFloat8=%zu",
        CACHE_LINE, PAGE_SIZE, NUMA_PAGE, ULTRA_PAGE, SIMD_ALIGNMENT,
        sizeof(CacheAlignedValue<u64>),
        sizeof(UltraAlignedData<u64>),
        sizeof(SimdFloat8)
    );
    
    return info;
}

extern "C" AARENDOCORE_API bool AARendoCore_CheckAlignment(const void* ptr, u64 alignment) {
    return IsAligned(ptr, static_cast<usize>(alignment));
}

// ============================================================================
// ALIGNED MEMORY ALLOCATION
// ============================================================================

extern "C" AARENDOCORE_API void* AARendoCore_AllocateAligned(u64 size, u64 alignment) {
    if (size == 0 || alignment == 0) {
        return nullptr;
    }
    
    // Ensure alignment is power of 2
    if ((alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
    
#if AARENDOCORE_PLATFORM_WINDOWS
    return _aligned_malloc(static_cast<size_t>(size), static_cast<size_t>(alignment));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, static_cast<size_t>(alignment), static_cast<size_t>(size)) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

extern "C" AARENDOCORE_API void AARendoCore_FreeAligned(void* ptr) {
    if (ptr == nullptr) {
        return;
    }
    
#if AARENDOCORE_PLATFORM_WINDOWS
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ============================================================================
// ALIGNMENT UTILITIES
// ============================================================================

extern "C" AARENDOCORE_API u64 AARendoCore_AlignUp(u64 value, u64 alignment) {
    return AlignUp(static_cast<usize>(value), static_cast<usize>(alignment));
}

extern "C" AARENDOCORE_API u64 AARendoCore_AlignDown(u64 value, u64 alignment) {
    return AlignDown(static_cast<usize>(value), static_cast<usize>(alignment));
}

AARENDOCORE_NAMESPACE_END