#include <cstddef>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "frame_encoder.h"
#include "frame_parser.h"

namespace {

// ========================================================
// Test Helpers
// ========================================================

std::vector<std::byte> payload_a() {
    return {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
        std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C},
        std::byte{0x0D}, std::byte{0x0E},
    };
}

std::vector<std::byte> payload_b() {
    return {
        std::byte{0x21}, std::byte{0x22}, std::byte{0x23}, std::byte{0x24},
        std::byte{0x25}, std::byte{0x26}, std::byte{0x27}, std::byte{0x28},
        std::byte{0x29}, std::byte{0x2A}, std::byte{0x2B}, std::byte{0x2C},
        std::byte{0x2D}, std::byte{0x2E}, std::byte{0x2F}, std::byte{0x30},
    };
}

std::vector<std::byte> payload_c() {
    return {
        std::byte{0x41}, std::byte{0x42}, std::byte{0x43}, std::byte{0x44},
        std::byte{0x45}, std::byte{0x46}, std::byte{0x47}, std::byte{0x48},
        std::byte{0x49}, std::byte{0x4A}, std::byte{0x4B}, std::byte{0x4C},
        std::byte{0x4D}, std::byte{0x4E}, std::byte{0x4F}, std::byte{0x50},
        std::byte{0x51}, std::byte{0x52}, std::byte{0x53}, std::byte{0x54},
    };
}

}  // namespace


// ========================================================
// Group A — Normal Parsing
// ========================================================
TEST(FrameParserTest, ParsesCompleteFrame) {
    // Arrange:
    const std::vector<std::byte> expected_payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
        std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C},
        std::byte{0x0D}, std::byte{0x0E},
    };

    const std::vector<std::byte> frame{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0E},
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
        std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C},
        std::byte{0x0D}, std::byte{0x0E},
    };

    FrameParser parser;

    // Act:
    const auto result = parser.feed(frame.data(), frame.size());

    // Assert:
    ASSERT_EQ(result.completed_payloads.size(), 1u);
    EXPECT_EQ(result.completed_payloads[0], expected_payload);
    EXPECT_FALSE(result.protocol_error.has_value());
}

TEST(FrameParserTest, HandlesFragmentedHeader) {
    // Arrange:
    const auto expected_payload = payload_a();
    const auto frame = encode_frame(expected_payload);

    const std::vector<std::byte> chunk_1(frame.begin(), frame.begin() + 2);
    const std::vector<std::byte> chunk_2(frame.begin() + 2, frame.end());

    FrameParser parser;

    // Act & Assert (first feed):
    const auto first_result = parser.feed(chunk_1.data(), chunk_1.size());
    EXPECT_TRUE(first_result.completed_payloads.empty());
    EXPECT_FALSE(first_result.protocol_error.has_value());

    // Act & Assert (second feed):
    const auto second_result = parser.feed(chunk_2.data(), chunk_2.size());
    ASSERT_EQ(second_result.completed_payloads.size(), 1u);
    EXPECT_EQ(second_result.completed_payloads[0], expected_payload);
    EXPECT_FALSE(second_result.protocol_error.has_value());
}

TEST(FrameParserTest, HandlesFragmentedPayload) {
    // Arrange:
    const auto expected_payload = payload_a();
    const auto frame = encode_frame(expected_payload);

    const std::vector<std::byte> chunk_1(frame.begin(), frame.begin() + 9);
    const std::vector<std::byte> chunk_2(frame.begin() + 9, frame.end());

    FrameParser parser;

    // Act & Assert (first feed):
    const auto first_result = parser.feed(chunk_1.data(), chunk_1.size());
    EXPECT_TRUE(first_result.completed_payloads.empty());
    EXPECT_FALSE(first_result.protocol_error.has_value());

    // Act & Assert (second feed):
    const auto second_result = parser.feed(chunk_2.data(), chunk_2.size());
    ASSERT_EQ(second_result.completed_payloads.size(), 1u);
    EXPECT_EQ(second_result.completed_payloads[0], expected_payload);
    EXPECT_FALSE(second_result.protocol_error.has_value());
}

TEST(FrameParserTest, ParsesMultipleFrames) {
    // Arrange:
    const auto expected_payload_a = payload_a();
    const auto expected_payload_b = payload_b();

    const auto frame_a = encode_frame(expected_payload_a);
    const auto frame_b = encode_frame(expected_payload_b);

    std::vector<std::byte> combined = frame_a;
    combined.insert(combined.end(), frame_b.begin(), frame_b.end());

    FrameParser parser;

    // Act:
    const auto result = parser.feed(combined.data(), combined.size());

    // Assert:
    ASSERT_EQ(result.completed_payloads.size(), 2u);
    EXPECT_EQ(result.completed_payloads[0], expected_payload_a);
    EXPECT_EQ(result.completed_payloads[1], expected_payload_b);
    EXPECT_FALSE(result.protocol_error.has_value());
}

TEST(FrameParserTest, PreservesIncompleteTrailingFrame) {
    // Arrange:
    const auto expected_payload_a = payload_a();
    const auto expected_payload_b = payload_b();
    const auto expected_payload_c = payload_c();

    const auto frame_a = encode_frame(expected_payload_a);
    const auto frame_b = encode_frame(expected_payload_b);
    const auto frame_c = encode_frame(expected_payload_c);

    std::vector<std::byte> stream = frame_a;
    stream.insert(stream.end(), frame_b.begin(), frame_b.end());
    stream.insert(stream.end(), frame_c.begin(), frame_c.end());

    const std::vector<std::byte> chunk_1(stream.begin(), stream.begin() + 45);
    const std::vector<std::byte> chunk_2(stream.begin() + 45, stream.end());

    FrameParser parser;

    // Act & Assert (first feed):
    const auto first_result = parser.feed(chunk_1.data(), chunk_1.size());

    ASSERT_EQ(first_result.completed_payloads.size(), 2u);
    EXPECT_EQ(first_result.completed_payloads[0], expected_payload_a);
    EXPECT_EQ(first_result.completed_payloads[1], expected_payload_b);
    EXPECT_FALSE(first_result.protocol_error.has_value());

    // Act & Assert (second feed):
    const auto second_result = parser.feed(chunk_2.data(), chunk_2.size());

    ASSERT_EQ(second_result.completed_payloads.size(), 1u);
    EXPECT_EQ(second_result.completed_payloads[0], expected_payload_c);
    EXPECT_FALSE(second_result.protocol_error.has_value());
}


// ========================================================
// Group B — Protocol Error
// ========================================================
TEST(FrameParserTest, RejectsTooShortLength) {
    // Arrange:
    const std::vector<std::byte> prefix{std::byte{0x00}, std::byte{0x00},
                                        std::byte{0x00}, std::byte{0x0D}};

    FrameParser parser;

    // Act:
    const auto result = parser.feed(prefix.data(), prefix.size());

    // Assert:
    EXPECT_TRUE(result.completed_payloads.empty());
    ASSERT_TRUE(result.protocol_error.has_value());
    EXPECT_EQ(*result.protocol_error, FrameError::InvalidLength);

    EXPECT_THROW(parser.feed(prefix.data(), prefix.size()), std::logic_error);
    EXPECT_THROW(parser.finish(), std::logic_error);
}

TEST(FrameParserTest, RejectsTooLongLength) {
    // Arrange:
    const std::vector<std::byte> prefix{std::byte{0x00}, std::byte{0x01},
                                        std::byte{0x00}, std::byte{0x0E}};

    FrameParser parser;

    // Act:
    const auto result = parser.feed(prefix.data(), prefix.size());

    // Assert:
    EXPECT_TRUE(result.completed_payloads.empty());
    ASSERT_TRUE(result.protocol_error.has_value());
    EXPECT_EQ(*result.protocol_error, FrameError::InvalidLength);
}

TEST(FrameParserTest, PreservesPayloadsBeforeTooShortLength) {
    // Arrange:
    const auto expected_payload_a = payload_a();
    const auto expected_payload_b = payload_b();

    const auto frame_a = encode_frame(expected_payload_a);
    const auto frame_b = encode_frame(expected_payload_b);

    const std::vector<std::byte> invalid_prefix{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0D}};

    std::vector<std::byte> stream = frame_a;
    stream.insert(stream.end(), frame_b.begin(), frame_b.end());
    stream.insert(stream.end(), invalid_prefix.begin(), invalid_prefix.end());

    FrameParser parser;

    // Act:
    const auto result = parser.feed(stream.data(), stream.size());

    // Assert:
    ASSERT_EQ(result.completed_payloads.size(), 2u);
    EXPECT_EQ(result.completed_payloads[0], expected_payload_a);
    EXPECT_EQ(result.completed_payloads[1], expected_payload_b);
    ASSERT_TRUE(result.protocol_error.has_value());
    EXPECT_EQ(*result.protocol_error, FrameError::InvalidLength);
}

TEST(FrameParserTest, PreservesPayloadsBeforeTooLongLength) {
    // Arrange:
    const auto expected_payload_a = payload_a();
    const auto expected_payload_b = payload_b();

    const auto frame_a = encode_frame(expected_payload_a);
    const auto frame_b = encode_frame(expected_payload_b);

    const std::vector<std::byte> invalid_prefix{
        std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x0E}};

    std::vector<std::byte> stream = frame_a;
    stream.insert(stream.end(), frame_b.begin(), frame_b.end());
    stream.insert(stream.end(), invalid_prefix.begin(), invalid_prefix.end());

    FrameParser parser;

    // Act:
    const auto result = parser.feed(stream.data(), stream.size());

    // Assert:
    ASSERT_EQ(result.completed_payloads.size(), 2u);
    EXPECT_EQ(result.completed_payloads[0], expected_payload_a);
    EXPECT_EQ(result.completed_payloads[1], expected_payload_b);
    ASSERT_TRUE(result.protocol_error.has_value());
    EXPECT_EQ(*result.protocol_error, FrameError::InvalidLength);
}

TEST(FrameParserTest, RejectsFragmentedInvalidHeader) {
    // Arrange:
    const auto expected_payload_a = payload_a();
    const auto frame_a = encode_frame(expected_payload_a);

    const std::vector<std::byte> invalid_prefix{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0D}};

    std::vector<std::byte> chunk_1 = frame_a;
    chunk_1.insert(chunk_1.end(), invalid_prefix.begin(),
                   invalid_prefix.begin() + 2);

    const std::vector<std::byte> chunk_2(invalid_prefix.begin() + 2,
                                         invalid_prefix.end());

    FrameParser parser;

    // Act & Assert (first feed):
    const auto first_result = parser.feed(chunk_1.data(), chunk_1.size());

    ASSERT_EQ(first_result.completed_payloads.size(), 1u);
    EXPECT_EQ(first_result.completed_payloads[0], expected_payload_a);
    EXPECT_FALSE(first_result.protocol_error.has_value());

    // Act & Assert (second feed):
    const auto second_result = parser.feed(chunk_2.data(), chunk_2.size());

    EXPECT_TRUE(second_result.completed_payloads.empty());
    ASSERT_TRUE(second_result.protocol_error.has_value());
    EXPECT_EQ(*second_result.protocol_error, FrameError::InvalidLength);

    EXPECT_THROW(parser.feed(chunk_2.data(), chunk_2.size()), std::logic_error);
}


// ========================================================
// Group C — EOF / Lifecycle
// ========================================================
TEST(FrameParserTest, CleanEOF) {
    // Arrange:
    const auto expected_payload = payload_a();
    const auto frame = encode_frame(expected_payload);

    FrameParser parser;

    // Act:
    const auto result = parser.feed(frame.data(), frame.size());

    // Assert (feed):
    ASSERT_EQ(result.completed_payloads.size(), 1u);
    EXPECT_EQ(result.completed_payloads[0], expected_payload);
    EXPECT_FALSE(result.protocol_error.has_value());

    // Act & Assert (EOF):
    const auto eof_error = parser.finish();
    EXPECT_FALSE(eof_error.has_value());

    EXPECT_THROW(parser.feed(frame.data(), frame.size()), std::logic_error);
    EXPECT_THROW(parser.finish(), std::logic_error);
}

TEST(FrameParserTest, EOFWithPartialHeader) {
    // Arrange:
    const std::vector<std::byte> partial_header{std::byte{0x00},
                                                std::byte{0x00}};

    FrameParser parser;

    // Act:
    const auto result =
        parser.feed(partial_header.data(), partial_header.size());

    // Assert (feed):
    EXPECT_TRUE(result.completed_payloads.empty());
    EXPECT_FALSE(result.protocol_error.has_value());

    // Act & Assert (EOF):
    const auto eof_error = parser.finish();

    ASSERT_TRUE(eof_error.has_value());
    EXPECT_EQ(*eof_error, FrameError::TruncatedFrame);

    EXPECT_THROW(parser.feed(partial_header.data(), partial_header.size()),
                 std::logic_error);
    EXPECT_THROW(parser.finish(), std::logic_error);
}

TEST(FrameParserTest, EOFWithPartialPayload) {
    // Arrange:
    const std::vector<std::byte> partial_frame{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x0E}, std::byte{0x01}, std::byte{0x02},
        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};

    FrameParser parser;

    // Act:
    const auto result = parser.feed(partial_frame.data(), partial_frame.size());

    // Assert (feed):
    EXPECT_TRUE(result.completed_payloads.empty());
    EXPECT_FALSE(result.protocol_error.has_value());

    // Act & Assert (EOF):
    const auto eof_error = parser.finish();

    ASSERT_TRUE(eof_error.has_value());
    EXPECT_EQ(*eof_error, FrameError::TruncatedFrame);

    EXPECT_THROW(parser.feed(partial_frame.data(), partial_frame.size()),
                 std::logic_error);
    EXPECT_THROW(parser.finish(), std::logic_error);
}
