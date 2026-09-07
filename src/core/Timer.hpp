#pragma once

#include <cstdint>

namespace elm::core {

    uint64_t getTimeStamp();
    double getMilliseconds(uint64_t start, uint64_t end);
    double getMicroseconds(uint64_t start, uint64_t end);
}