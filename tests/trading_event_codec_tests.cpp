#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "trading_event_codec.h"


TEST(EventCodecTest, EncodeProducesKnownPayloadBytes) {
    // Setup:
    const TradingEventMessage event{0x01020304, 0x02030405, 0x03040506, "ABCD"};
    std::vector<std::byte> buffer{};

    // Encode:
    buffer = encode_trading_event(event);

    // Verify (network byte order):
    const std::vector<std::byte> expected{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},

        std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05},

        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06},

        std::byte{0x00}, std::byte{0x04},

        std::byte{'A'},  std::byte{'B'},  std::byte{'C'},  std::byte{'D'}};

    ASSERT_EQ(buffer.size(), expected.size());
    EXPECT_EQ(buffer, expected);
}

TEST(EventCodecTest, EncodeDecodeRoundTrip) {
    // Setup:
    const TradingEventMessage event{0x01020304, 0x02030405, 0x03040506, "ABCD"};

    // Encode & Decode:
    auto ret = decode_trading_event(encode_trading_event(event));

    // Verify:
    EXPECT_EQ(ret.type, event.type);
    EXPECT_EQ(ret.price_ticks, event.price_ticks);
    EXPECT_EQ(ret.quantity, event.quantity);
    EXPECT_EQ(ret.symbol, event.symbol);
}

TEST(EventCodecTest, EncodeDecodeEmptySymbol) {
    // Setup:
    const TradingEventMessage event{0x01020304, 0x02030405, 0x03040506, ""};

    // Encode & Decode:
    auto buffer = encode_trading_event(event);
    auto ret = decode_trading_event(buffer);

    // Verify:
    EXPECT_EQ(buffer.size(), 14);
    EXPECT_EQ(ret.type, event.type);
    EXPECT_EQ(ret.price_ticks, event.price_ticks);
    EXPECT_EQ(ret.quantity, event.quantity);
    EXPECT_EQ(ret.symbol.size(), 0);
}

TEST(EventCodecTest, DecodeRejectsTruncatedFixedFields) {
    // Setup:
    const std::vector<std::byte> buffer{std::byte{0x01}, std::byte{0x02}};

    // Decode & Verify:
    EXPECT_THROW(decode_trading_event(buffer), std::out_of_range);
}

TEST(EventCodecTest, DecodeRejectsTruncatedSymbol) {
    // Setup:
    const std::vector<std::byte> buffer{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},

        std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05},

        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06},

        std::byte{0x00}, std::byte{0x05},  // declare symbol_length = 5 > 4

        std::byte{'A'},  std::byte{'B'},  std::byte{'C'},  std::byte{'D'}};

    // Decode & Verify:
    EXPECT_THROW(decode_trading_event(buffer), std::out_of_range);
}

TEST(EventCodecTest, DecodeRejectsTrailingBytes) {
    // Setup:
    const std::vector<std::byte> buffer{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},

        std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05},

        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06},

        std::byte{0x00}, std::byte{0x03},  // declare symbol_length = 3 < 4

        std::byte{'A'},  std::byte{'B'},  std::byte{'C'},  std::byte{'D'}};

    // Decode & Verify:
    EXPECT_THROW(decode_trading_event(buffer), std::out_of_range);
}

TEST(EventCodecTest, EncodeAcceptsMaximumSymbol) {
    // Setup:
    const std::string symbol(std::numeric_limits<std::uint16_t>::max(), 'A');
    const TradingEventMessage event{1, 2, 3, symbol};

    // Encode & Decode:
    auto ret = decode_trading_event(encode_trading_event(event));

    // Verify:
    EXPECT_EQ(ret.type, event.type);
    EXPECT_EQ(ret.price_ticks, event.price_ticks);
    EXPECT_EQ(ret.quantity, event.quantity);
    EXPECT_EQ(ret.symbol, event.symbol);
}

TEST(EventCodecTest, EncodeRejectsOversizedSymbol) {
    // Setup:
    const std::string symbol(std::numeric_limits<std::uint16_t>::max() + 1,
                             'A');
    const TradingEventMessage event{1, 2, 3, symbol};

    // Encode & Verify:
    EXPECT_THROW(encode_trading_event(event), std::length_error);
}
