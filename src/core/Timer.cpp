#include "core/Timer.hpp"

#include <chrono>

namespace elm::core {
    uint64_t getTimeStamp() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }   

    double getMilliseconds(uint64_t start, uint64_t end) {  
        return static_cast<double>(end - start) / 1'000'000.0;
    }

    double getMicroseconds(uint64_t start, uint64_t end) {
        return static_cast<double>(end - start) / 1'000.0;
    }
}
