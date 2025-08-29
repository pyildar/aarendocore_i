//===--- AARendoCore.cpp - Main DLL Entry Point -------------------------===//
//
// COMPILATION LEVEL: 10 (Top level)
// ORIGIN: Main entry point for AARendoCoreGLM.dll
//
// DLL exports and initialization with PSYCHOTIC PRECISION.
//===----------------------------------------------------------------------===//

#include "Core_Platform.h"
#include "Core_Types.h"
#include "Core_StreamSynchronizer.h"
#include "check_sizes.h"
#include <windows.h>
#include <iostream>

// DLL Entry Point
BOOL APIENTRY DllMain([[maybe_unused]] HMODULE hModule, DWORD ul_reason_for_call, [[maybe_unused]] LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // PSYCHOTIC VERIFICATION ON LOAD
        #ifdef _DEBUG
        AARendoCoreGLM::CheckStructSizes();
        #endif
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// Export initialization function
extern "C" AARENDOCORE_API int AARendoCore_Initialize() {
    // Check sizes in debug mode
    #ifdef _DEBUG
    AARendoCoreGLM::CheckStructSizes();
    #endif
    
    return 0; // Success
}

// Export version info
extern "C" AARENDOCORE_API const char* AARendoCore_GetVersion() {
    return "1.0.0-PSYCHOTIC";
}