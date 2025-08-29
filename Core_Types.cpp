// Core_Types.cpp - FUNDAMENTAL TYPE IMPLEMENTATIONS
// Validates that our types compile and work correctly

#include "Core_Types.h"
#include <cstdio>  // For std::snprintf

AARENDOCORE_NAMESPACE_BEGIN

// ============================================================================
// TYPE VALIDATION - Runtime checks for our type system
// ============================================================================

namespace {
    // Static initialization that validates our types at program start
    struct TypeValidator {
        TypeValidator() {
            // Validate atomic types are lock-free at runtime
            AARENDOCORE_ASSERT(AtomicU64{}.is_lock_free());
            AARENDOCORE_ASSERT(AtomicI64{}.is_lock_free());
            AARENDOCORE_ASSERT(AtomicU32{}.is_lock_free());
            AARENDOCORE_ASSERT(AtomicI32{}.is_lock_free());
            
            // Validate memory constants
            AARENDOCORE_ASSERT(CACHE_LINE == 64);
            AARENDOCORE_ASSERT(PAGE_SIZE == 4096);
            AARENDOCORE_ASSERT(ULTRA_PAGE == 2048);
            
            // Validate size calculations
            AARENDOCORE_ASSERT(KB == 1024);
            AARENDOCORE_ASSERT(MB == 1024 * 1024);
            AARENDOCORE_ASSERT(GB == 1024 * 1024 * 1024);
            
            // Test Result type
            Result<u64> okResult(42);
            AARENDOCORE_ASSERT(okResult.isOk());
            AARENDOCORE_ASSERT(okResult.unwrap() == 42);
            
            Result<void> voidResult;
            AARENDOCORE_ASSERT(voidResult.isOk());
            
            // Test SessionId
            SessionId id1(100);
            SessionId id2(200);
            AARENDOCORE_ASSERT(id1 != id2);
            AARENDOCORE_ASSERT(id1 < id2);
            AARENDOCORE_ASSERT(id1 == SessionId(100));
        }
    };
    
    // This runs at program startup
    [[maybe_unused]] static TypeValidator validator;
}

// ============================================================================
// EXPORTED TYPE INFORMATION
// ============================================================================

extern "C" AARENDOCORE_API const char* AARendoCore_GetTypeInfo() {
    static char info[512];
    
    // Use snprintf safely
    std::snprintf(info, sizeof(info),
        "Type Sizes: u64=%zu, f64=%zu, SessionId=%zu, AtomicU64=%zu | "
        "Alignments: CacheLine=%zu, Page=%zu, UltraPage=%zu | "
        "Lock-free: U64=%d, I64=%d",
        sizeof(u64), sizeof(f64), sizeof(SessionId), sizeof(AtomicU64),
        CACHE_LINE, PAGE_SIZE, ULTRA_PAGE,
        AtomicU64{}.is_lock_free() ? 1 : 0,
        AtomicI64{}.is_lock_free() ? 1 : 0
    );
    
    return info;
}

// ============================================================================
// SESSION ID GENERATION - Critical for our system
// ============================================================================

// Global atomic counter for session IDs
static AtomicU64 g_nextSessionId{1};  // Start at 1, 0 is invalid

extern "C" AARENDOCORE_API u64 AARendoCore_GenerateSessionId() {
    // Atomic increment with memory ordering
    return g_nextSessionId.fetch_add(1, std::memory_order_relaxed);
}

extern "C" AARENDOCORE_API bool AARendoCore_ValidateSessionId(u64 id) {
    // 0 is invalid, and we shouldn't exceed what we've generated
    return id > 0 && id < g_nextSessionId.load(std::memory_order_acquire);
}

AARENDOCORE_NAMESPACE_END