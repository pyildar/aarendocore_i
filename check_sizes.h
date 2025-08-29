#ifndef AARENDOCORE_CHECK_SIZES_H
#define AARENDOCORE_CHECK_SIZES_H

#include <iostream>
#include <cstddef>  // For offsetof

// Runtime size checking with PSYCHOTIC PRECISION
namespace AARendoCoreGLM {

inline void CheckStructSizes() {
    std::cout << "\n=== PSYCHOTIC SIZE VERIFICATION ===\n";
    
    std::cout << "Order size: " << sizeof(Order) << " bytes (need 32)\n";
    #pragma warning(push)
    #pragma warning(disable: 4127) // conditional expression is constant
    if (sizeof(Order) != 32) {
        std::cout << "  ERROR: Order is " << (sizeof(Order) - 32) << " bytes too big!\n";
    }
    
    std::cout << "StreamState size: " << sizeof(StreamState) << " bytes (need <= 128)\n";
    if (sizeof(StreamState) > 128) {
        std::cout << "  ERROR: StreamState is " << (sizeof(StreamState) - 128) << " bytes too big!\n";
    }
    
    std::cout << "SynchronizerConfig size: " << sizeof(SynchronizerConfig) << " bytes (need 64)\n";
    if (sizeof(SynchronizerConfig) != 64) {
        std::cout << "  ERROR: SynchronizerConfig is off by " << (int)(sizeof(SynchronizerConfig) - 64) << " bytes!\n";
    }
    
    std::cout << "StreamSynchronizer size: " << sizeof(StreamSynchronizer) << " bytes (need <= 8192)\n";
    if (sizeof(StreamSynchronizer) > 8192) {
        std::cout << "  ERROR: StreamSynchronizer is " << (sizeof(StreamSynchronizer) - 8192) << " bytes too big!\n";
    }
    #pragma warning(pop)
    
    std::cout << "\n=== FIELD OFFSETS ===\n";
    Order o{};
    std::cout << "Order.orderId offset: " << offsetof(Order, orderId) << "\n";
    std::cout << "Order.price offset: " << offsetof(Order, price) << "\n";
    std::cout << "Order.quantity offset: " << offsetof(Order, quantity) << "\n";
    std::cout << "Order.type offset: " << offsetof(Order, type) << "\n";
    std::cout << "Order._pad offset: " << offsetof(Order, _pad) << "\n";
}

} // namespace AARendoCoreGLM

#endif // AARENDOCORE_CHECK_SIZES_H