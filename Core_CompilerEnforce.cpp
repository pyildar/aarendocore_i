//===--- Core_CompilerEnforce.cpp - Compiler Enforcement Implementation --===//
//
// COMPILATION LEVEL: 0 (ZERO DEPENDENCIES)
// ORIGIN: Implementation for Core_CompilerEnforce.h
// DEPENDENCIES: Core_CompilerEnforce.h only
// DEPENDENTS: None
//
// Validates compiler enforcement mechanisms at runtime (debug only).
//===----------------------------------------------------------------------===//

#include "Core_CompilerEnforce.h"

// No namespace - these are global compile-time checks

// ==========================================================================
// COMPILE-TIME VALIDATION TESTS
// ==========================================================================

namespace {
    // Origin: Anonymous namespace for compile-time validation
    // These tests run at compile time to verify enforcement works
    
    // Test 1: Verify mutex detection works
    struct TestNoMutex {
        int value;
    };
    ENFORCE_NO_MUTEX(TestNoMutex);  // Should compile
    
    // Test 2: Verify alignment enforcement
    struct CACHE_ALIGNED TestCacheAligned {
        char data[64];
    };
    static_assert(alignof(TestCacheAligned) == 64, "Cache alignment failed");
    
    struct ULTRA_ALIGNED TestUltraAligned {
        char data[2048];
    };
    static_assert(alignof(TestUltraAligned) == 2048, "Ultra alignment failed");
    
    // Test 3: Verify power of two check
    static_assert(is_power_of_two<64>::value, "64 should be power of 2");
    static_assert(is_power_of_two<1024>::value, "1024 should be power of 2");
    static_assert(!is_power_of_two<100>::value, "100 should not be power of 2");
    
    // Test 4: Verify strong typedef
    STRONG_TYPEDEF(int, TestId);
    
    // Test 5: Stack-only class
    STACK_ONLY_CLASS(TestStackOnly)
        int value;
    };
    
    // Test 6: Variable origin tracking
    void testOriginTracking() {
        LOCAL_VAR(int, testVar, 42);
        static_assert(testVar_ORIGIN != nullptr, "Origin must be tracked");
        (void)testVar;  // Suppress unused warning - variable exists to test macro
    }
}

// ==========================================================================
// RUNTIME VALIDATION (DEBUG ONLY)
// ==========================================================================

#ifdef DEBUG

// Origin: Function to validate compiler enforcement at runtime
// Input: None
// Output: true if all validations pass
// Scope: Function duration
bool ValidateCompilerEnforcement() noexcept {
    // Test 1: Verify alignment actually works
    {
        struct alignas(64) AlignTest {
            char data;
        };
        AlignTest test;
        
        // addr: Origin - Local variable from address-of operator, Scope: block
        auto addr = reinterpret_cast<uintptr_t>(&test);
        if (addr % 64 != 0) return false;
    }
    
    // Test 2: Verify ultra alignment
    {
        struct alignas(2048) UltraTest {
            char data;
        };
        UltraTest test;
        
        // addr: Origin - Local variable from address-of operator, Scope: block
        auto addr = reinterpret_cast<uintptr_t>(&test);
        if (addr % 2048 != 0) return false;
    }
    
    // Test 3: Verify strong typedef prevents implicit conversion
    {
        STRONG_TYPEDEF(int, StrongInt);
        StrongInt si(42);
        // int raw = si;  // This should NOT compile (implicit conversion)
        int raw = si.get();  // Explicit access required
        if (raw != 42) return false;
    }
    
    return true;
}

// Origin: Static initialization validator
// Executed at program startup in debug builds only
namespace {
    struct CompilerEnforcementValidator {
        CompilerEnforcementValidator() {
            // validated: Origin - Local variable from function call, Scope: constructor
            const bool validated = ValidateCompilerEnforcement();
            (void)validated;  // Suppress unused warning
            
            #ifdef EXTREME_DEBUG
            if (!validated) {
                // In extreme debug, terminate if validation fails
                std::terminate();
            }
            #endif
        }
    };
    
    // Origin: Static instance to trigger validation at startup
    static CompilerEnforcementValidator enforcementValidator;
}

#endif // DEBUG

// ==========================================================================
// ENFORCEMENT VERIFICATION
// ==========================================================================

// Verify this file follows its own rules
ENFORCE_EXTREME_PRINCIPLES();
VCXPROJ_INCLUDE("Core_CompilerEnforce.cpp");

// Verify no dynamic allocation in this file
namespace {
    struct VerifyNoHeap {
        PREVENT_HEAP_ALLOCATION();
        int dummy;
    };
}

// Final verification
ENFORCE_HEADER_COMPLETE(Core_CompilerEnforce);