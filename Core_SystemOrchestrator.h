//===--- Core_SystemOrchestrator.h - Central System Coordinator --------===//
//
// COMPILATION LEVEL: 8 (HIGHEST - Coordinates ALL components)
// ORIGIN: NEW - Central brain that connects all components
// DEPENDENCIES: Core_Types.h, Core_Atomic.h (forward declares everything else)
// DEPENDENTS: Main entry point, DLL exports
//
// The BRAIN of the entire trading system - coordinates all components
// with ZERO ownership coupling and MAXIMUM flexibility.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_SYSTEMORCHESTRATOR_H
#define AARENDOCORE_CORE_SYSTEMORCHESTRATOR_H

#include "Core_Platform.h"
#include "Core_PrimitiveTypes.h"
#include "Core_Types.h"
#include "Core_Atomic.h"

// PSYCHOTIC PRECISION: Forward declarations ONLY - ZERO coupling!
namespace AARendoCoreGLM {
    class ProcessingUnitFactory;
    class SessionManager;
    class DAGExecutor;
    class ThreadPool;
    struct SystemConfig;
}

namespace AARendoCoreGLM {

// ============================================================================
// SYSTEM STATE - Orchestrator lifecycle state
// ============================================================================

enum class SystemState : u32 {
    UNINITIALIZED = 0,
    INITIALIZING  = 1,
    READY         = 2,  
    RUNNING       = 3,
    PAUSING       = 4,
    PAUSED        = 5,
    SHUTTING_DOWN = 6,
    TERMINATED    = 7,
    ERROR         = 8
};

// ============================================================================
// SYSTEM CONFIGURATION - Minimal config for Phase 1
// ============================================================================

struct SystemConfig {
    // Basic system parameters
    u32 maxSessions;           // Default: 10'000'000
    u32 workerThreads;         // Default: CPU cores
    u32 numaNodes;             // Default: detected
    
    // Processing unit limits
    u32 maxTickUnits;          // Default: 1000
    u32 maxDataUnits;          // Default: 1000
    u32 maxBatchUnits;         // Default: 500
    u32 maxOrderUnits;         // Default: 500
    
    // Memory configuration
    u64 totalMemoryMB;         // Default: 8GB
    u32 cacheLineSize;         // Default: 64
    
    SystemConfig() noexcept;
    void setDefaults() noexcept;
    bool validate() const noexcept;
};

// ============================================================================
// SYSTEM STATISTICS - Performance metrics
// ============================================================================

struct alignas(CACHE_LINE_SIZE) SystemStats {
    AtomicU64 totalSessions;
    AtomicU64 activeSessions;
    AtomicU64 totalProcessingUnits;
    AtomicU64 ticksProcessed;
    AtomicU64 ordersProcessed;
    AtomicU64 systemUptime;
    mutable AtomicU32 currentState;  // Mutable: updated in const getStats()
    
    SystemStats() noexcept;
    void reset() noexcept;
};

// ============================================================================
// SYSTEM ORCHESTRATOR - The Central Brain
// ============================================================================

class AARENDOCORE_API SystemOrchestrator {
private:
    // System state
    std::atomic<SystemState> state_;
    SystemConfig config_;
    SystemStats stats_;
    
    // Component interfaces (NOT owned - injected!)
    ProcessingUnitFactory* factory_;
    SessionManager* sessionManager_;
    DAGExecutor* dagExecutor_;
    ThreadPool* threadPool_;
    
    // Component lifecycle flags
    AtomicBool factoryInitialized_;
    AtomicBool sessionManagerInitialized_;
    AtomicBool dagExecutorInitialized_;
    AtomicBool threadPoolInitialized_;
    
public:
    SystemOrchestrator() noexcept;
    ~SystemOrchestrator() noexcept;
    
    // Disable copy/move - Singleton pattern
    SystemOrchestrator(const SystemOrchestrator&) = delete;
    SystemOrchestrator& operator=(const SystemOrchestrator&) = delete;
    SystemOrchestrator(SystemOrchestrator&&) = delete;
    SystemOrchestrator& operator=(SystemOrchestrator&&) = delete;
    
    // ========================================================================
    // PHASE 1: BASIC LIFECYCLE - Minimal but comprehensive
    // ========================================================================
    
    // Initialize system with configuration
    ResultCode initialize(const SystemConfig& config) noexcept;
    
    // Start the trading system
    ResultCode start() noexcept;
    
    // Pause system (keep state, stop processing)
    ResultCode pause() noexcept;
    
    // Resume from pause
    ResultCode resume() noexcept;
    
    // Shutdown system gracefully
    ResultCode shutdown() noexcept;
    
    // ========================================================================
    // COMPONENT INTEGRATION - Wire everything together
    // ========================================================================
    
    // Initialize ProcessingUnitFactory
    ResultCode initializeFactory() noexcept;
    
    // Initialize SessionManager with factory
    ResultCode initializeSessionManager() noexcept;
    
    // Initialize DAGExecutor
    ResultCode initializeDAGExecutor() noexcept;
    
    // Initialize ThreadPool
    ResultCode initializeThreadPool() noexcept;
    
    // ========================================================================
    // SYSTEM QUERIES - State and statistics
    // ========================================================================
    
    SystemState getState() const noexcept;
    const SystemStats& getStats() const noexcept;
    const SystemConfig& getConfig() const noexcept;
    
    bool isInitialized() const noexcept;
    bool isRunning() const noexcept;
    bool isPaused() const noexcept;
    
    // Component availability
    bool hasFactory() const noexcept;
    bool hasSessionManager() const noexcept;
    bool hasDAGExecutor() const noexcept;
    bool hasThreadPool() const noexcept;
    
    // ========================================================================
    // MINIMAL API - Essential operations only
    // ========================================================================
    
    // Create a new trading session
    SessionId createSession(const char* accountId, const char* strategyName) noexcept;
    
    // Destroy a trading session
    bool destroySession(SessionId sessionId) noexcept;
    
    // Get session count
    u64 getSessionCount() const noexcept;
    
    // ========================================================================
    // DEBUGGING - Minimal diagnostic info
    // ========================================================================
    
    void dumpState() const noexcept;
    const char* getStateString() const noexcept;
    
private:
    // Internal state management
    bool transitionState(SystemState newState) noexcept;
    void updateStats() noexcept;
    
    // Component initialization helpers
    ResultCode createComponents() noexcept;
    void destroyComponents() noexcept;
    
    // Validation
    bool validateConfig(const SystemConfig& config) const noexcept;
};

// ============================================================================
// GLOBAL ORCHESTRATOR INSTANCE - Singleton access
// ============================================================================

// Get the global orchestrator instance (create if needed)
SystemOrchestrator* GetSystemOrchestrator() noexcept;

// Initialize global system
ResultCode InitializeSystem(const SystemConfig& config) noexcept;

// Shutdown global system
void ShutdownSystem() noexcept;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char* SystemStateToString(SystemState state) noexcept;
SystemConfig GetDefaultConfig() noexcept;

} // namespace AARendoCoreGLM

#endif // AARENDOCORE_CORE_SYSTEMORCHESTRATOR_H