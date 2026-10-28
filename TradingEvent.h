#pragma once

#include <cstdint>
#include <string>

// For use by thread performance test:
struct TradingEvent {
    std::uint64_t sequence;
    int payload;
};

// For use by codec test:
struct TradingEventMessage {
    std::uint32_t type;
    std::uint32_t price_ticks;
    std::uint32_t quantity;
    std::string symbol;  // ...[symbol_length][symbol]
};
