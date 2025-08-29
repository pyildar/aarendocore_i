// Core_Alignment.h - MEMORY ALIGNMENT SPECIFICATIONS
// COMPILER PROCESSES FOURTH - Final piece of foundation layer
// Controls how the compiler lays out ALL our structures in memory

#ifndef AARENDOCOREGLM_CORE_ALIGNMENT_H
#define AARENDOCOREGLM_CORE_ALIGNMENT_H

#include "Core_Platform.h"  // Foundation
#include "Core_Types.h"     // Type definitions
#include "Core_Config.h"    // Configuration constants

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// ALIGNMENT MACROS - Tell compiler EXACTLY how to align our data
// ============================================================================

// Cache line alignment - prevents false sharing
#define CACHE_ALIGNED alignas(CACHE_LINE)

// Page alignment - for OS page boundaries
#define PAGE_ALIGNED alignas(PAGE_SIZE)

// NUMA page alignment - for huge pages
#define NUMA_ALIGNED alignas(NUMA_PAGE)

// Ultra alignment - our psychotic 2KB alignment
#define ULTRA_ALIGNED alignas(ULTRA_PAGE)

// SIMD alignment - for AVX2 operations
#define SIMD_ALIGNED alignas(SIMD_ALIGNMENT)

// ============================================================================
// PADDING HELPERS - Explicit padding to prevent compiler surprises
// ============================================================================

// Calculate padding needed to reach alignment
template<usize CurrentSize, usize TargetAlignment>
constexpr usize PaddingNeeded = 
    (TargetAlignment - (CurrentSize % TargetAlignment)) % TargetAlignment;

// Padding array type
template<usize Bytes>
using Padding = byte[Bytes];

// ============================================================================
// ALIGNED STORAGE - For creating aligned buffers
// ============================================================================

template<typename T, usize Alignment = alignof(T)>
struct AlignedStorage {
    alignas(Alignment) byte data[sizeof(T)];
    
    T* get() noexcept { 
        return reinterpret_cast<T*>(data); 
    }
    
    const T* get() const noexcept { 
        return reinterpret_cast<const T*>(data); 
    }
    
    T& ref() noexcept { 
        return *get(); 
    }
    
    const T& ref() const noexcept { 
        return *get(); 
    }
};

// ============================================================================
// CACHE-OPTIMIZED STRUCTURES - Prevent false sharing
// ============================================================================

// Single producer, single consumer aligned value
template<typename T>
struct CACHE_ALIGNED CacheAlignedValue {
    T value;
    Padding<PaddingNeeded<sizeof(T), CACHE_LINE>> _padding;
    
    CacheAlignedValue() = default;
    explicit CacheAlignedValue(const T& v) : value(v) {}
    
    operator T() const noexcept { return value; }
    T* operator->() noexcept { return &value; }
    const T* operator->() const noexcept { return &value; }
};

// Atomic value with cache line isolation
template<typename T>
struct CACHE_ALIGNED CacheAlignedAtomic {
    std::atomic<T> value;
    Padding<PaddingNeeded<sizeof(std::atomic<T>), CACHE_LINE>> _padding;
    
    CacheAlignedAtomic() = default;
    explicit CacheAlignedAtomic(T v) : value(v) {}
    
    T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return value.load(order);
    }
    
    void store(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        value.store(v, order);
    }
    
    T fetch_add(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return value.fetch_add(v, order);
    }
};

// ============================================================================
// NUMA-ALIGNED STRUCTURES - For NUMA-aware allocation
// ============================================================================

template<typename T>
struct NUMA_ALIGNED NumaAlignedBuffer {
    static constexpr usize CAPACITY = NUMA_PAGE / sizeof(T);
    T data[CAPACITY];
    
    constexpr usize capacity() const noexcept { return CAPACITY; }
    T& operator[](usize idx) noexcept { return data[idx]; }
    const T& operator[](usize idx) const noexcept { return data[idx]; }
};

// ============================================================================
// ULTRA-ALIGNED STRUCTURES - Our psychotic 2KB alignment
// ============================================================================

template<typename T>
struct ULTRA_ALIGNED UltraAlignedData {
    T data;
    Padding<PaddingNeeded<sizeof(T), ULTRA_PAGE>> _padding;
    
    UltraAlignedData() = default;
    explicit UltraAlignedData(const T& v) : data(v) {}
    
    T* operator->() noexcept { return &data; }
    const T* operator->() const noexcept { return &data; }
    T& operator*() noexcept { return data; }
    const T& operator*() const noexcept { return data; }
};

// ============================================================================
// SIMD-ALIGNED STRUCTURES - For vectorized operations
// ============================================================================

template<typename T, usize Count>
struct SIMD_ALIGNED SimdVector {
    static_assert(sizeof(T) * Count <= SIMD_REGISTER_SIZE / 8,
        "SimdVector too large for SIMD register");
    
    T data[Count];
    
    T& operator[](usize idx) noexcept { return data[idx]; }
    const T& operator[](usize idx) const noexcept { return data[idx]; }
};

// Specialized for common types
using SimdFloat4 = SimdVector<f32, 4>;
using SimdFloat8 = SimdVector<f32, 8>;
using SimdDouble2 = SimdVector<f64, 2>;
using SimdDouble4 = SimdVector<f64, 4>;

// ============================================================================
// ALIGNMENT UTILITIES - Helper functions
// ============================================================================

// Check if pointer is aligned
template<typename T>
AARENDOCORE_FORCEINLINE bool IsAligned(const T* ptr, usize alignment) noexcept {
    return (reinterpret_cast<uptr>(ptr) & (alignment - 1)) == 0;
}

// Align value up to boundary
AARENDOCORE_FORCEINLINE constexpr usize AlignUp(usize value, usize alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Align value down to boundary
AARENDOCORE_FORCEINLINE constexpr usize AlignDown(usize value, usize alignment) noexcept {
    return value & ~(alignment - 1);
}

// Get alignment of type
template<typename T>
constexpr usize AlignmentOf = alignof(T);

// ============================================================================
// STATIC ASSERTIONS - Verify alignment properties
// ============================================================================

// Verify our aligned structures are correctly sized
static_assert(sizeof(CacheAlignedValue<u64>) == CACHE_LINE,
    "CacheAlignedValue must be exactly one cache line");

static_assert(sizeof(CacheAlignedAtomic<u64>) == CACHE_LINE,
    "CacheAlignedAtomic must be exactly one cache line");

static_assert(sizeof(UltraAlignedData<u64>) == ULTRA_PAGE,
    "UltraAlignedData must be exactly one ultra page");

// Verify alignment values are powers of 2
static_assert(IsPowerOfTwo<CACHE_LINE>, "Cache line must be power of 2");
static_assert(IsPowerOfTwo<PAGE_SIZE>, "Page size must be power of 2");
static_assert(IsPowerOfTwo<NUMA_PAGE>, "NUMA page must be power of 2");
static_assert(IsPowerOfTwo<ULTRA_PAGE>, "Ultra page must be power of 2");
static_assert(IsPowerOfTwo<SIMD_ALIGNMENT>, "SIMD alignment must be power of 2");

// Verify SIMD vectors are properly aligned
static_assert(alignof(SimdFloat8) == SIMD_ALIGNMENT,
    "SimdFloat8 must be SIMD aligned");
static_assert(alignof(SimdDouble4) == SIMD_ALIGNMENT,
    "SimdDouble4 must be SIMD aligned");

AARENDOCORE_NAMESPACE_END

#endif // AARENDOCOREGLM_CORE_ALIGNMENT_H