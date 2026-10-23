#include "trading_event_codec.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "TradingEvent.h"
#include "binary_codec.h"

std::vector<std::byte> encode_trading_event(const TradingEventMessage& event) {
    // setup:
    std::vector<std::byte> buffer{};

    // check symbol_length:
    if (event.symbol.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::length_error("symbol is too long");
    }

    // append fixed payload:
    append_u32(buffer, event.type);
    append_u32(buffer, event.price_ticks);
    append_u32(buffer, event.quantity);
    append_u16(buffer, static_cast<std::uint16_t>(event.symbol.size()));

    // resize for symbol:
    const auto old_size = buffer.size();
    buffer.resize(old_size + event.symbol.size());

    // append symbol:
    if (!event.symbol.empty()) {
        std::memcpy(buffer.data() + old_size, event.symbol.data(),
                    event.symbol.size());
    }

    return buffer;
}

TradingEventMessage
decode_trading_event(const std::vector<std::byte>& payload) {
    // Setup:
    TradingEventMessage event{};
    std::size_t offset{};
    std::uint16_t symbol_length{};

    // Read fixed payload:
    event.type = read_u32(payload, offset);
    event.price_ticks = read_u32(payload, offset);
    event.quantity = read_u32(payload, offset);

    // Read variable symbol_length:
    symbol_length = read_u16(payload, offset);

    // Check symbol_length:
    if (payload.size() - offset != symbol_length) {
        throw std::out_of_range("mismatched symbol_length");
    }

    // Read symbol:
    event.symbol.resize(symbol_length);
    if (symbol_length != 0) {
        std::memcpy(event.symbol.data(), payload.data() + offset,
                    symbol_length);
    }

    return event;
}
