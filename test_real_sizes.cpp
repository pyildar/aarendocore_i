#include <iostream>
#include <cstddef>

// Simulate the actual types
struct alignas(8) AtomicU64 { unsigned long long value; };
struct alignas(8) AtomicF64 { double value; };

struct alignas(32) Tick {
    unsigned long long timestamp;
    double price;
    double volume;
    unsigned int flags;
    char padding[4];
};

struct alignas(64) Bar {
    unsigned long long timestamp;
    double open;
    double high;
    double low;
    double close;
    double volume;
    unsigned int tickCount;
    char padding[4];
};

enum class FillStrategy : unsigned char {
    OLD_TICK = 0
};

// Test StreamState
struct alignas(64) StreamState {
    AtomicU64 latestTimestamp;
    AtomicU64 lastCompletedBarTime;
    Tick lastTick;
    Bar lastCompletedBar;
    FillStrategy currentStrategy;
    bool isLeader;
    bool isSynchronized;
    bool hasGap;
    char padding[76];
};

// Test SynchronizerConfig
struct alignas(64) SynchronizerConfig {
    unsigned long long bufferWindowNs;
    unsigned long long maxLagNs;
    unsigned int leaderMode;
    bool enableAVX2;
    bool enableCorrelation;
    bool enableAdaptive;
    unsigned char _padding1;
    unsigned int maxStreams;
    double syncFrequency;
    char padding[92];
};

int main() {
    std::cout << "=== STRUCT SIZES ===\n";
    std::cout << "AtomicU64: " << sizeof(AtomicU64) << " bytes\n";
    std::cout << "Tick: " << sizeof(Tick) << " bytes\n";
    std::cout << "Bar: " << sizeof(Bar) << " bytes\n";
    std::cout << "FillStrategy: " << sizeof(FillStrategy) << " bytes\n";
    std::cout << "bool: " << sizeof(bool) << " bytes\n";
    
    std::cout << "\n=== CALCULATED SIZES ===\n";
    std::cout << "StreamState components:\n";
    std::cout << "  2x AtomicU64: " << (2 * sizeof(AtomicU64)) << " bytes\n";
    std::cout << "  1x Tick: " << sizeof(Tick) << " bytes\n";
    std::cout << "  1x Bar: " << sizeof(Bar) << " bytes\n";
    std::cout << "  1x FillStrategy: " << sizeof(FillStrategy) << " bytes\n";
    std::cout << "  3x bool: " << (3 * sizeof(bool)) << " bytes\n";
    std::cout << "  Total before padding: " << 
        (2 * sizeof(AtomicU64) + sizeof(Tick) + sizeof(Bar) + 
         sizeof(FillStrategy) + 3 * sizeof(bool)) << " bytes\n";
    std::cout << "  With 76 bytes padding: " << sizeof(StreamState) << " bytes\n";
    std::cout << "  Target (3x64): 192 bytes\n";
    
    std::cout << "\nSynchronizerConfig components:\n";
    std::cout << "  2x u64: " << (2 * 8) << " bytes\n";
    std::cout << "  1x u32: " << 4 << " bytes\n";
    std::cout << "  3x bool: " << (3 * sizeof(bool)) << " bytes\n";
    std::cout << "  1x u8: " << 1 << " bytes\n";
    std::cout << "  1x u32: " << 4 << " bytes\n";
    std::cout << "  1x f64: " << 8 << " bytes\n";
    std::cout << "  Total before padding: " << (16 + 4 + 3 + 1 + 4 + 8) << " bytes\n";
    std::cout << "  With 92 bytes padding: " << sizeof(SynchronizerConfig) << " bytes\n";
    std::cout << "  Target (2x64): 128 bytes\n";
    
    // Check alignments
    std::cout << "\n=== FIELD OFFSETS ===\n";
    std::cout << "StreamState:\n";
    std::cout << "  latestTimestamp: " << offsetof(StreamState, latestTimestamp) << "\n";
    std::cout << "  lastCompletedBarTime: " << offsetof(StreamState, lastCompletedBarTime) << "\n";
    std::cout << "  lastTick: " << offsetof(StreamState, lastTick) << "\n";
    std::cout << "  lastCompletedBar: " << offsetof(StreamState, lastCompletedBar) << "\n";
    std::cout << "  currentStrategy: " << offsetof(StreamState, currentStrategy) << "\n";
    std::cout << "  padding: " << offsetof(StreamState, padding) << "\n";
    
    return 0;
}