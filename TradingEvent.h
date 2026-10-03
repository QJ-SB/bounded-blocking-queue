#pragma once

#include <cstdint>

struct TradingEvent {
    std::uint64_t sequence;
    int payload;
};
