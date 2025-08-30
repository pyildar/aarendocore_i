//===--- Core_AVX2Math.h - AVX2 SIMD Math Operations --------------------===//
//
// COMPILATION LEVEL: 1 (Depends on PrimitiveTypes only)
// ORIGIN: NEW - AVX2 optimized math for ALIEN LEVEL performance
// DEPENDENCIES: Core_PrimitiveTypes.h
// DEPENDENTS: All units needing SIMD optimization
//
// PSYCHOTIC SIMD performance, ZERO branches, NANOSECOND operations.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_AVX2MATH_H
#define AARENDOCORE_CORE_AVX2MATH_H

#include "Core_PrimitiveTypes.h"
#include "Core_CompilerEnforce.h"
#include <immintrin.h>
#include <cmath>

namespace AARendoCoreGLM {

// ==========================================================================
// AVX2 CONSTANTS - PSYCHOTIC PRECISION
// ==========================================================================

// Origin: Constant - Epsilon for floating point comparison, Scope: Global
alignas(32) constexpr f64 AVX2_EPSILON[4] = {1e-15, 1e-15, 1e-15, 1e-15};

// Origin: Constant - One vector, Scope: Global
alignas(32) constexpr f64 AVX2_ONE[4] = {1.0, 1.0, 1.0, 1.0};

// Origin: Constant - Zero vector, Scope: Global
alignas(32) constexpr f64 AVX2_ZERO[4] = {0.0, 0.0, 0.0, 0.0};

// ==========================================================================
// AVX2 MATH OPERATIONS - ALIEN LEVEL
// ==========================================================================

// Origin: Class for AVX2 optimized math operations
class AVX2Math {
public:
    // ======================================================================
    // VECTOR OPERATIONS
    // ======================================================================
    
    // Origin: Add two vectors with AVX2
    // Input: a, b - Vectors to add
    // Output: Result vector
    static FORCE_INLINE __m256d add(__m256d a, __m256d b) noexcept {
        return _mm256_add_pd(a, b);
    }
    
    // Origin: Subtract vectors with AVX2
    // Input: a, b - Vectors (a - b)
    // Output: Result vector
    static FORCE_INLINE __m256d sub(__m256d a, __m256d b) noexcept {
        return _mm256_sub_pd(a, b);
    }
    
    // Origin: Multiply vectors with AVX2
    // Input: a, b - Vectors to multiply
    // Output: Result vector
    static FORCE_INLINE __m256d mul(__m256d a, __m256d b) noexcept {
        return _mm256_mul_pd(a, b);
    }
    
    // Origin: Divide vectors with AVX2
    // Input: a, b - Vectors (a / b)
    // Output: Result vector
    static FORCE_INLINE __m256d div(__m256d a, __m256d b) noexcept {
        return _mm256_div_pd(a, b);
    }
    
    // Origin: Fused multiply-add (a * b + c)
    // Input: a, b, c - Vectors
    // Output: Result vector
    static FORCE_INLINE __m256d fma(__m256d a, __m256d b, __m256d c) noexcept {
        return _mm256_fmadd_pd(a, b, c);
    }
    
    // ======================================================================
    // COMPARISON OPERATIONS
    // ======================================================================
    
    // Origin: Compare greater than
    // Input: a, b - Vectors to compare
    // Output: Mask vector
    static FORCE_INLINE __m256d cmpgt(__m256d a, __m256d b) noexcept {
        return _mm256_cmp_pd(a, b, _CMP_GT_OQ);
    }
    
    // Origin: Compare less than
    // Input: a, b - Vectors to compare
    // Output: Mask vector
    static FORCE_INLINE __m256d cmplt(__m256d a, __m256d b) noexcept {
        return _mm256_cmp_pd(a, b, _CMP_LT_OQ);
    }
    
    // Origin: Compare equal with epsilon
    // Input: a, b - Vectors to compare
    // Output: Mask vector
    static FORCE_INLINE __m256d cmpeq(__m256d a, __m256d b) noexcept {
        // diff: Origin - Local calculation, Scope: function
        __m256d diff = _mm256_sub_pd(a, b);
        // abs_diff: Origin - Local calculation, Scope: function
        __m256d abs_diff = _mm256_andnot_pd(_mm256_set1_pd(-0.0), diff);
        // epsilon: Origin - Local constant load, Scope: function
        __m256d epsilon = _mm256_load_pd(AVX2_EPSILON);
        return _mm256_cmp_pd(abs_diff, epsilon, _CMP_LE_OQ);
    }
    
    // ======================================================================
    // REDUCTION OPERATIONS
    // ======================================================================
    
    // Origin: Horizontal sum of vector elements
    // Input: v - Vector to sum
    // Output: Sum of all elements
    static FORCE_INLINE f64 hsum(__m256d v) noexcept {
        // hi: Origin - Local extraction, Scope: function
        __m128d hi = _mm256_extractf128_pd(v, 1);
        // lo: Origin - Local extraction, Scope: function
        __m128d lo = _mm256_castpd256_pd128(v);
        lo = _mm_add_pd(lo, hi);
        hi = _mm_shuffle_pd(lo, lo, 1);
        lo = _mm_add_pd(lo, hi);
        return _mm_cvtsd_f64(lo);
    }
    
    // Origin: Horizontal product of vector elements
    // Input: v - Vector to multiply
    // Output: Product of all elements
    static FORCE_INLINE f64 hprod(__m256d v) noexcept {
        // hi: Origin - Local extraction, Scope: function
        __m128d hi = _mm256_extractf128_pd(v, 1);
        // lo: Origin - Local extraction, Scope: function
        __m128d lo = _mm256_castpd256_pd128(v);
        lo = _mm_mul_pd(lo, hi);
        hi = _mm_shuffle_pd(lo, lo, 1);
        lo = _mm_mul_pd(lo, hi);
        return _mm_cvtsd_f64(lo);
    }
    
    // Origin: Horizontal maximum
    // Input: v - Vector
    // Output: Maximum element
    static FORCE_INLINE f64 hmax(__m256d v) noexcept {
        // hi: Origin - Local extraction, Scope: function
        __m128d hi = _mm256_extractf128_pd(v, 1);
        // lo: Origin - Local extraction, Scope: function
        __m128d lo = _mm256_castpd256_pd128(v);
        lo = _mm_max_pd(lo, hi);
        hi = _mm_shuffle_pd(lo, lo, 1);
        lo = _mm_max_pd(lo, hi);
        return _mm_cvtsd_f64(lo);
    }
    
    // Origin: Horizontal minimum
    // Input: v - Vector
    // Output: Minimum element
    static FORCE_INLINE f64 hmin(__m256d v) noexcept {
        // hi: Origin - Local extraction, Scope: function
        __m128d hi = _mm256_extractf128_pd(v, 1);
        // lo: Origin - Local extraction, Scope: function
        __m128d lo = _mm256_castpd256_pd128(v);
        lo = _mm_min_pd(lo, hi);
        hi = _mm_shuffle_pd(lo, lo, 1);
        lo = _mm_min_pd(lo, hi);
        return _mm_cvtsd_f64(lo);
    }
    
    // ======================================================================
    // MATHEMATICAL FUNCTIONS
    // ======================================================================
    
    // Origin: Square root with AVX2
    // Input: v - Vector
    // Output: Square root of each element
    static FORCE_INLINE __m256d sqrt(__m256d v) noexcept {
        return _mm256_sqrt_pd(v);
    }
    
    // Origin: Reciprocal with Newton-Raphson refinement
    // Input: v - Vector
    // Output: 1/v for each element
    static FORCE_INLINE __m256d reciprocal(__m256d v) noexcept {
        // Initial approximation
        // one: Origin - Local constant load, Scope: function
        __m256d one = _mm256_set1_pd(1.0);
        // two: Origin - Local constant, Scope: function
        __m256d two = _mm256_set1_pd(2.0);
        
        // x0: Origin - Local initial approximation, Scope: function
        __m256d x0 = _mm256_div_pd(one, v);
        
        // Newton-Raphson iteration: x1 = x0 * (2 - v * x0)
        // vx0: Origin - Local calculation, Scope: function
        __m256d vx0 = _mm256_mul_pd(v, x0);
        // two_minus_vx0: Origin - Local calculation, Scope: function
        __m256d two_minus_vx0 = _mm256_sub_pd(two, vx0);
        // x1: Origin - Local refined result, Scope: function
        __m256d x1 = _mm256_mul_pd(x0, two_minus_vx0);
        
        return x1;
    }
    
    // Origin: Absolute value
    // Input: v - Vector
    // Output: Absolute value of each element
    static FORCE_INLINE __m256d abs(__m256d v) noexcept {
        // signmask: Origin - Local constant, Scope: function
        __m256d signmask = _mm256_set1_pd(-0.0);
        return _mm256_andnot_pd(signmask, v);
    }
    
    // ======================================================================
    // LOAD/STORE OPERATIONS
    // ======================================================================
    
    // Origin: Aligned load
    // Input: ptr - Aligned pointer to data
    // Output: Loaded vector
    static FORCE_INLINE __m256d load_aligned(const f64* ptr) noexcept {
        return _mm256_load_pd(ptr);
    }
    
    // Origin: Unaligned load
    // Input: ptr - Pointer to data
    // Output: Loaded vector
    static FORCE_INLINE __m256d load_unaligned(const f64* ptr) noexcept {
        return _mm256_loadu_pd(ptr);
    }
    
    // Origin: Aligned store
    // Input: ptr - Aligned pointer, v - Vector to store
    static FORCE_INLINE void store_aligned(f64* ptr, __m256d v) noexcept {
        _mm256_store_pd(ptr, v);
    }
    
    // Origin: Unaligned store
    // Input: ptr - Pointer, v - Vector to store
    static FORCE_INLINE void store_unaligned(f64* ptr, __m256d v) noexcept {
        _mm256_storeu_pd(ptr, v);
    }
    
    // Origin: Broadcast single value to all elements
    // Input: value - Value to broadcast
    // Output: Vector with all elements set to value
    static FORCE_INLINE __m256d broadcast(f64 value) noexcept {
        return _mm256_set1_pd(value);
    }
    
    // ======================================================================
    // SPECIAL OPERATIONS
    // ======================================================================
    
    // Origin: Blend vectors based on mask
    // Input: a, b - Vectors, mask - Selection mask
    // Output: Blended vector
    static FORCE_INLINE __m256d blend(__m256d a, __m256d b, __m256d mask) noexcept {
        return _mm256_blendv_pd(a, b, mask);
    }
    
    // Origin: Gather from non-contiguous memory
    // Input: base - Base pointer, indices - Offsets
    // Output: Gathered vector
    static FORCE_INLINE __m256d gather(const f64* base, __m128i indices) noexcept {
        return _mm256_i32gather_pd(base, indices, 8);
    }
    
    // Origin: Permute vector elements
    // Input: v - Vector, Control - Permutation control (compile-time constant)
    // Output: Permuted vector
    template<int Control>
    static FORCE_INLINE __m256d permute(__m256d v) noexcept {
        return _mm256_permute4x64_pd(v, Control);
    }
};

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify AVX2 is available
#ifndef __AVX2__
    #error "AVX2 support required for Core_AVX2Math.h"
#endif

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_AVX2Math);

} // namespace AARendoCoreGLM

#endif // AARENDOCORE_CORE_AVX2MATH_H