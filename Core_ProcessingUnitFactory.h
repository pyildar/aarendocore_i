//===--- Core_ProcessingUnitFactory.h - MINIMAL Factory for Phase 1 ----===//
//
// COMPILATION LEVEL: 6 (After processing units, before orchestrator)
// ORIGIN: NEW - Minimal factory for creating processing units
// DEPENDENCIES: Core_Types.h, Core_DAGTypes.h, IProcessingUnit.h
// DEPENDENTS: SystemOrchestrator
//
// PHASE 1: MINIMAL but functional factory
// PHASE 2: Add advanced features (pools, contexts, etc.)
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_PROCESSINGUNITFACTORY_H
#define AARENDOCORE_CORE_PROCESSINGUNITFACTORY_H

#include "Core_Platform.h"
#include "Core_PrimitiveTypes.h"
#include "Core_Types.h"
#include "Core_DAGTypes.h"
#include "Core_Atomic.h"

namespace AARendoCoreGLM {

// Forward declarations
class IProcessingUnit;

// ============================================================================
// FACTORY CONFIGURATION - Minimal for Phase 1
// ============================================================================

struct FactoryConfig {
    u32 maxUnitsPerType;       // Default: 1000
    u32 initialPoolSize;       // Default: 100  
    i32 numaNode;              // Default: -1 (any node)
    
    FactoryConfig() noexcept;
    void setDefaults() noexcept;
    bool validate() const noexcept;
};

// ============================================================================
// FACTORY STATISTICS - Basic metrics
// ============================================================================

struct FactoryStats {
    AtomicU64 totalUnitsCreated;
    AtomicU64 totalUnitsDestroyed;
    AtomicU64 activeUnits;
    AtomicU32 tickUnits;
    AtomicU32 dataUnits; 
    AtomicU32 batchUnits;
    AtomicU32 orderUnits;
    
    FactoryStats() noexcept;
    void reset() noexcept;
};

// ============================================================================
// PROCESSING UNIT FACTORY - MINIMAL Phase 1 Implementation
// ============================================================================

class AARENDOCORE_API ProcessingUnitFactory {
private:
    // Factory state
    std::atomic<bool> initialized_;
    FactoryConfig config_;
    FactoryStats stats_;
    
    // Simple counters (Phase 2 will add pools)
    AtomicU64 nextUnitId_;
    
public:
    ProcessingUnitFactory() noexcept;
    ~ProcessingUnitFactory() noexcept;
    
    // Disable copy/move - Singleton pattern
    ProcessingUnitFactory(const ProcessingUnitFactory&) = delete;
    ProcessingUnitFactory& operator=(const ProcessingUnitFactory&) = delete;
    
    // ========================================================================
    // PHASE 1: BASIC LIFECYCLE
    // ========================================================================
    
    ResultCode initialize(const FactoryConfig& config) noexcept;
    ResultCode shutdown() noexcept;
    
    bool isInitialized() const noexcept;
    
    // ========================================================================
    // PHASE 1: MINIMAL UNIT CREATION
    // ========================================================================
    
    // Create processing unit by type
    IProcessingUnit* createUnit(ProcessingUnitType type, 
                               i32 numaNode = -1) noexcept;
    
    // Destroy processing unit
    bool destroyUnit(IProcessingUnit* unit) noexcept;
    
    // ========================================================================
    // SPECIFIC UNIT CREATORS - What SessionManager needs
    // ========================================================================
    
    IProcessingUnit* createTickProcessor(i32 numaNode = -1) noexcept;
    IProcessingUnit* createDataProcessor(i32 numaNode = -1) noexcept;
    IProcessingUnit* createBatchProcessor(i32 numaNode = -1) noexcept;
    IProcessingUnit* createInterpolationProcessor(i32 numaNode = -1) noexcept;
    
    // PHASE 1: Stub for OrderProcessor (will implement in Step 4)
    IProcessingUnit* createOrderProcessor(i32 numaNode = -1) noexcept;
    
    // ========================================================================
    // FACTORY QUERIES
    // ========================================================================
    
    const FactoryStats& getStats() const noexcept;
    const FactoryConfig& getConfig() const noexcept;
    
    u64 getActiveUnitCount() const noexcept;
    u32 getUnitCount(ProcessingUnitType type) const noexcept;
    
private:
    // Internal helpers
    ProcessingUnitId generateUnitId() noexcept;
    void updateStats(ProcessingUnitType type, bool created) noexcept;
    bool validateUnitType(ProcessingUnitType type) const noexcept;
};

// ============================================================================
// GLOBAL FACTORY INSTANCE - Singleton access
// ============================================================================

// Get global factory instance
ProcessingUnitFactory* GetProcessingUnitFactory() noexcept;

// Initialize global factory
ResultCode InitializeProcessingUnitFactory(const FactoryConfig& config) noexcept;

// Shutdown global factory
void ShutdownProcessingUnitFactory() noexcept;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

FactoryConfig GetDefaultFactoryConfig() noexcept;

} // namespace AARendoCoreGLM

#endif // AARENDOCORE_CORE_PROCESSINGUNITFACTORY_H