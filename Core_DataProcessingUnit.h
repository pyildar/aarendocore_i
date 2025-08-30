//===--- Core_DataProcessingUnit.h - Generic Data Processing Unit -------===//
//
// COMPILATION LEVEL: 4 (Depends on BaseProcessingUnit)
// ORIGIN: NEW - Generic data processing implementation
// DEPENDENCIES: Core_BaseProcessingUnit.h
// DEPENDENTS: None
//
// Generic data processor with PSYCHOTIC flexibility.
// Handles any data type with ZERO overhead.
//===----------------------------------------------------------------------===//

#ifndef AARENDOCORE_CORE_DATAPROCESSINGUNIT_H
#define AARENDOCORE_CORE_DATAPROCESSINGUNIT_H

#include "Core_BaseProcessingUnit.h"
#include "Core_LockFreeQueue.h"
#include "Core_Config.h"

// Enforce compilation level
#ifndef CORE_DATAPROCESSINGUNIT_LEVEL_DEFINED
#define CORE_DATAPROCESSINGUNIT_LEVEL_DEFINED
static constexpr int DataProcessingUnit_CompilationLevel = 4;
#endif

AARENDOCORE_NAMESPACE_BEGIN

// ==========================================================================
// DATA PROCESSING CONFIGURATION
// ==========================================================================

// Origin: Structure for data processing configuration
// Scope: Passed during initialization
struct alignas(CACHE_LINE_SIZE) DataProcessingConfig {
    // Origin: Member - Data type identifier, Scope: Config lifetime
    u32 dataTypeId;
    
    // Origin: Member - Processing mode flags, Scope: Config lifetime
    u32 processingMode;
    
    // Origin: Member - Buffer size for data, Scope: Config lifetime
    u32 bufferSize;
    
    // Origin: Member - Batch size for processing, Scope: Config lifetime
    u32 batchSize;
    
    // Origin: Member - Enable compression, Scope: Config lifetime
    bool enableCompression;
    
    // Origin: Member - Enable validation, Scope: Config lifetime
    bool enableValidation;
    
    // Origin: Member - Enable caching, Scope: Config lifetime
    bool enableCaching;
    
    // Origin: Member - Cache size in bytes, Scope: Config lifetime
    u64 cacheSize;
    
    // Origin: Member - Timeout in nanoseconds, Scope: Config lifetime
    u64 timeoutNs;
    
    // Padding to cache line
    char padding[13];
};

static_assert(sizeof(DataProcessingConfig) == CACHE_LINE_SIZE,
              "DataProcessingConfig must be exactly one cache line");

// ==========================================================================
// DATA PROCESSING UNIT - GENERIC IMPLEMENTATION
// ==========================================================================

// Origin: Generic data processing unit
// Handles any data type with ZERO overhead
class alignas(ULTRA_PAGE_SIZE) DataProcessingUnit final : public BaseProcessingUnit {
public:
    // ======================================================================
    // PUBLIC CONSTANTS
    // ======================================================================
    
    // Origin: Constant - Maximum buffer size, Scope: Compile-time
    static constexpr u32 MAX_BUFFER_SIZE = 65536;  // 64K power of 2
    
    // Origin: Constant - Maximum batch size, Scope: Compile-time
    static constexpr u32 MAX_BATCH_SIZE = 1024;    // 1K batch

private:
    // ======================================================================
    // MEMBER VARIABLES
    // ======================================================================
    
    // Origin: Member - Data configuration, Scope: Instance lifetime
    DataProcessingConfig dataConfig_;
    
    // Origin: Member - Generic data buffer, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) u8* dataBuffer_;
    
    // Origin: Member - Current buffer position, Scope: Instance lifetime
    AtomicU32 bufferPos_;
    
    // Origin: Member - Data queue for async processing, Scope: Instance lifetime
    LockFreeQueue<u64, MAX_BATCH_SIZE>* dataQueue_;
    
    // Origin: Member - Cache for processed data, Scope: Instance lifetime
    alignas(CACHE_LINE_SIZE) u8* cacheBuffer_;
    
    // Origin: Member - Cache position, Scope: Instance lifetime
    AtomicU32 cachePos_;
    
    // Origin: Member - Processing statistics, Scope: Instance lifetime
    AtomicU64 itemsProcessed_;
    AtomicU64 bytesProcessed_;
    AtomicU64 errorsCount_;
    
    // ======================================================================
    // PRIVATE METHODS
    // ======================================================================
    
    // Origin: Process generic data
    // Input: data - Data pointer, size - Data size
    // Output: true if processed
    bool processData(const void* data, usize size) noexcept;
    
    // Origin: Validate data
    // Input: data - Data to validate, size - Size
    // Output: true if valid
    bool validateData(const void* data, usize size) const noexcept;
    
    // Origin: Compress data
    // Input: src - Source data, srcSize - Source size
    //        dst - Destination buffer, dstSize - Buffer size
    // Output: Compressed size or 0 on error
    usize compressData(const void* src, usize srcSize,
                      void* dst, usize dstSize) noexcept;
    
    // Origin: Cache data
    // Input: data - Data to cache, size - Size
    // Output: true if cached
    bool cacheData(const void* data, usize size) noexcept;

public:
    // ======================================================================
    // CONSTRUCTOR/DESTRUCTOR
    // ======================================================================
    
    // Origin: Constructor
    explicit DataProcessingUnit(i32 numaNode = -1) noexcept;
    
    // Origin: Destructor
    virtual ~DataProcessingUnit() noexcept;
    
    // ======================================================================
    // IPROCESSINGUNIT IMPLEMENTATION
    // ======================================================================
    
    // Origin: Process single tick (converts to generic data)
    ProcessResult processTick(SessionId sessionId, const Tick& tick) noexcept override;
    
    // Origin: Process batch of ticks
    ProcessResult processBatch(SessionId sessionId, 
                               const Tick* ticks, 
                               usize count) noexcept override;
    
    // Origin: Process stream data
    ProcessResult processStream(SessionId sessionId,
                               const StreamData& streamData) noexcept override;
    
    // ======================================================================
    // DATA-SPECIFIC METHODS
    // ======================================================================
    
    // Origin: Configure data processing
    // Input: config - Data configuration
    // Output: ResultCode
    ResultCode configureData(const DataProcessingConfig& config) noexcept;
    
    // Origin: Process raw data
    // Input: data - Raw data pointer, size - Size
    // Output: ProcessResult
    ProcessResult processRawData(const void* data, usize size) noexcept;
    
    // Origin: Get processing statistics
    // Output: items processed, bytes processed, errors
    void getStatistics(u64& items, u64& bytes, u64& errors) const noexcept;
    
    // Origin: Clear buffers
    void clearBuffers() noexcept;
    
    // Origin: Flush cached data
    // Output: Number of items flushed
    u32 flushCache() noexcept;
    
private:
    // Padding to ensure ultra alignment
    char padding_[768];  // Adjust for ULTRA_PAGE_SIZE
};

static_assert(sizeof(DataProcessingUnit) <= ULTRA_PAGE_SIZE * 2,
              "DataProcessingUnit must fit in two ultra pages");

AARENDOCORE_NAMESPACE_END

// ==========================================================================
// COMPILE-TIME VALIDATION
// ==========================================================================

// Verify no mutex usage
ENFORCE_NO_MUTEX(AARendoCore::DataProcessingUnit);
ENFORCE_NO_MUTEX(AARendoCore::DataProcessingConfig);

// Mark header complete
ENFORCE_HEADER_COMPLETE(Core_DataProcessingUnit);

#endif // AARENDOCORE_CORE_DATAPROCESSINGUNIT_H