#pragma once

#include <cstddef>
#include <vector>

#include "TradingEvent.h"

std::vector<std::byte> encode_trading_event(const TradingEventMessage& event);

TradingEventMessage decode_trading_event(const std::vector<std::byte>& payload);
