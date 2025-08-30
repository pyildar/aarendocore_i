//===--- Core_ProcessingUnitFactory.cpp - Factory Implementation --------===//
//
// PHASE 1: MINIMAL but functional implementation
//===----------------------------------------------------------------------===//

#include "Core_ProcessingUnitFactory.h"
#include "Core_IProcessingUnit.h"
#include "Core_TickProcessingUnit.h"
#include "Core_DataProcessingUnit.h"
#include "Core_BatchProcessingUnit.h"
#include "Core_InterpolationProcessingUnit.h"

namespace AARendoCoreGLM {

// ============================================================================
// FACTORY CONFIGURATION IMPLEMENTATION
// ============================================================================

FactoryConfig::FactoryConfig() noexcept {
    setDefaults();
}

void FactoryConfig::setDefaults() noexcept {
    maxUnitsPerType = 1000;
    initialPoolSize = 100;
    numaNode = -1;  // Any node
}

bool FactoryConfig::validate() const noexcept {
    return maxUnitsPerType > 0 && 
           maxUnitsPerType <= 10000 &&  // Sanity limit
           initialPoolSize > 0 &&
           initialPoolSize <= maxUnitsPerType;
}

// ============================================================================
// FACTORY STATISTICS IMPLEMENTATION
// ============================================================================

FactoryStats::FactoryStats() noexcept {
    reset();
}

void FactoryStats::reset() noexcept {
    totalUnitsCreated.store(0, std::memory_order_relaxed);
    totalUnitsDestroyed.store(0, std::memory_order_relaxed);
    activeUnits.store(0, std::memory_order_relaxed);
    tickUnits.store(0, std::memory_order_relaxed);
    dataUnits.store(0, std::memory_order_relaxed);
    batchUnits.store(0, std::memory_order_relaxed);
    orderUnits.store(0, std::memory_order_relaxed);
}

// ============================================================================
// PROCESSING UNIT FACTORY IMPLEMENTATION
// ============================================================================

ProcessingUnitFactory::ProcessingUnitFactory() noexcept
    : initialized_(false)
    , config_{}
    , stats_{}
    , nextUnitId_(1) {  // Start from 1, 0 is invalid
}

ProcessingUnitFactory::~ProcessingUnitFactory() noexcept {
    shutdown();
}

ResultCode ProcessingUnitFactory::initialize(const FactoryConfig& config) noexcept {
    if (initialized_.load()) {
        return ResultCode::ERROR_ALREADY_INITIALIZED;
    }
    
    if (!config.validate()) {
        return ResultCode::ERROR_INVALID_PARAMETER;
    }
    
    config_ = config;
    stats_.reset();
    
    initialized_.store(true, std::memory_order_release);
    return ResultCode::SUCCESS;
}

ResultCode ProcessingUnitFactory::shutdown() noexcept {
    if (!initialized_.load()) {
        return ResultCode::SUCCESS; // Already shut down
    }
    
    // PHASE 1: Simple shutdown - just mark as uninitialized
    // PHASE 2: Will add proper cleanup of pools
    
    initialized_.store(false, std::memory_order_release);
    return ResultCode::SUCCESS;
}

bool ProcessingUnitFactory::isInitialized() const noexcept {
    return initialized_.load(std::memory_order_acquire);
}

// ============================================================================
// UNIT CREATION IMPLEMENTATION - PHASE 1 MINIMAL
// ============================================================================

IProcessingUnit* ProcessingUnitFactory::createUnit(ProcessingUnitType type, 
                                                  i32 numaNode) noexcept {
    if (!isInitialized()) {
        return nullptr;
    }
    
    if (!validateUnitType(type)) {
        return nullptr;
    }
    
    // Use provided NUMA node or factory default
    i32 targetNode = (numaNode >= 0) ? numaNode : config_.numaNode;
    
    IProcessingUnit* unit = nullptr;
    
    // PHASE 1: Direct instantiation (no pools yet)
    switch (type) {
        case ProcessingUnitType::MARKET_DATA_RECEIVER:
            unit = new TickProcessingUnit(targetNode);
            updateStats(type, true);
            stats_.tickUnits.fetch_add(1, std::memory_order_relaxed);
            break;
            
        case ProcessingUnitType::STREAM_NORMALIZER:
            unit = new DataProcessingUnit(targetNode);
            updateStats(type, true);
            stats_.dataUnits.fetch_add(1, std::memory_order_relaxed);
            break;
            
        case ProcessingUnitType::AGGREGATOR:
            unit = new BatchProcessingUnit(targetNode);
            updateStats(type, true);
            stats_.batchUnits.fetch_add(1, std::memory_order_relaxed);
            break;
            
        case ProcessingUnitType::INTERPOLATOR:
            unit = new InterpolationProcessingUnit(targetNode);
            updateStats(type, true);
            // Note: No specific counter for interpolation units yet
            break;
            
        // PHASE 1: Stubs for missing units
        case ProcessingUnitType::SIGNAL_GENERATOR:
        case ProcessingUnitType::RISK_EVALUATOR:
        case ProcessingUnitType::POSITION_SIZER:
        case ProcessingUnitType::ORDER_ROUTER:
        case ProcessingUnitType::PERSISTENCE_WRITER:
        case ProcessingUnitType::ALERT_DISPATCHER:
        case ProcessingUnitType::RESULT_PUBLISHER:
            // TODO: Implement these in Phase 2
            return nullptr;
            
        default:
            return nullptr;
    }
    
    return unit;
}

bool ProcessingUnitFactory::destroyUnit(IProcessingUnit* unit) noexcept {
    if (!unit || !isInitialized()) {
        return false;
    }
    
    // PHASE 1: Direct deletion (no pool return)
    // TODO PHASE 2: Return to appropriate pool
    
    // Note: We don't know the type from the unit pointer in Phase 1
    // This will be improved in Phase 2 with proper tracking
    
    delete unit;
    
    stats_.totalUnitsDestroyed.fetch_add(1, std::memory_order_relaxed);
    stats_.activeUnits.fetch_sub(1, std::memory_order_relaxed);
    
    return true;
}

// ============================================================================
// SPECIFIC UNIT CREATORS - What SessionManager needs
// ============================================================================

IProcessingUnit* ProcessingUnitFactory::createTickProcessor(i32 numaNode) noexcept {
    return createUnit(ProcessingUnitType::MARKET_DATA_RECEIVER, numaNode);
}

IProcessingUnit* ProcessingUnitFactory::createDataProcessor(i32 numaNode) noexcept {
    return createUnit(ProcessingUnitType::STREAM_NORMALIZER, numaNode);
}

IProcessingUnit* ProcessingUnitFactory::createBatchProcessor(i32 numaNode) noexcept {
    return createUnit(ProcessingUnitType::AGGREGATOR, numaNode);
}

IProcessingUnit* ProcessingUnitFactory::createInterpolationProcessor(i32 numaNode) noexcept {
    return createUnit(ProcessingUnitType::INTERPOLATOR, numaNode);
}

IProcessingUnit* ProcessingUnitFactory::createOrderProcessor(i32 numaNode) noexcept {
    // PHASE 1: Return nullptr - will implement OrderProcessingUnit in Step 4
    (void)numaNode;  // Suppress unused parameter warning
    return nullptr;
}

// ============================================================================
// FACTORY QUERIES IMPLEMENTATION
// ============================================================================

const FactoryStats& ProcessingUnitFactory::getStats() const noexcept {
    return stats_;
}

const FactoryConfig& ProcessingUnitFactory::getConfig() const noexcept {
    return config_;
}

u64 ProcessingUnitFactory::getActiveUnitCount() const noexcept {
    return stats_.activeUnits.load(std::memory_order_relaxed);
}

u32 ProcessingUnitFactory::getUnitCount(ProcessingUnitType type) const noexcept {
    switch (type) {
        case ProcessingUnitType::MARKET_DATA_RECEIVER:
            return stats_.tickUnits.load(std::memory_order_relaxed);
        case ProcessingUnitType::STREAM_NORMALIZER:
            return stats_.dataUnits.load(std::memory_order_relaxed);
        case ProcessingUnitType::AGGREGATOR:
            return stats_.batchUnits.load(std::memory_order_relaxed);
        // PHASE 1: Add more as needed
        default:
            return 0;
    }
}

// ============================================================================
// PRIVATE IMPLEMENTATION
// ============================================================================

ProcessingUnitId ProcessingUnitFactory::generateUnitId() noexcept {
    return nextUnitId_.fetch_add(1, std::memory_order_relaxed);
}

void ProcessingUnitFactory::updateStats(ProcessingUnitType type, bool created) noexcept {
    (void)type;  // Will use this in Phase 2 for per-type stats
    
    if (created) {
        stats_.totalUnitsCreated.fetch_add(1, std::memory_order_relaxed);
        stats_.activeUnits.fetch_add(1, std::memory_order_relaxed);
    }
}

bool ProcessingUnitFactory::validateUnitType(ProcessingUnitType type) const noexcept {
    // PHASE 1: Accept types we can create
    switch (type) {
        case ProcessingUnitType::MARKET_DATA_RECEIVER:
        case ProcessingUnitType::STREAM_NORMALIZER:
        case ProcessingUnitType::AGGREGATOR:
        case ProcessingUnitType::INTERPOLATOR:
            return true;
            
        // PHASE 1: Reject types we haven't implemented yet
        default:
            return false;
    }
}

// ============================================================================
// GLOBAL FACTORY INSTANCE IMPLEMENTATION
// ============================================================================

static ProcessingUnitFactory* g_factory = nullptr;

ProcessingUnitFactory* GetProcessingUnitFactory() noexcept {
    if (!g_factory) {
        g_factory = new ProcessingUnitFactory();
    }
    return g_factory;
}

ResultCode InitializeProcessingUnitFactory(const FactoryConfig& config) noexcept {
    ProcessingUnitFactory* factory = GetProcessingUnitFactory();
    if (!factory) {
        return ResultCode::ERROR_OUT_OF_MEMORY;
    }
    
    return factory->initialize(config);
}

void ShutdownProcessingUnitFactory() noexcept {
    if (g_factory) {
        g_factory->shutdown();
        delete g_factory;
        g_factory = nullptr;
    }
}

// ============================================================================
// UTILITY FUNCTIONS IMPLEMENTATION
// ============================================================================

FactoryConfig GetDefaultFactoryConfig() noexcept {
    return FactoryConfig{};
}

} // namespace AARendoCoreGLM