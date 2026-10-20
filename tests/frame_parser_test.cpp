#include <cstddef>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "frame_encoder.h"
#include "frame_parser.h"

namespace {

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
    const auto results = parser.feed(frame.data(), frame.size());

    // Assert:
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], expected_payload);
}

TEST(FrameParserTest, HandlesFragmentedHeader) {
    // Arrange:
    const auto expected_payload = payload_a();
    const auto frame = encode_frame(expected_payload);

    const std::vector<std::byte> chunk_1(frame.begin(), frame.begin() + 2);
    const std::vector<std::byte> chunk_2(frame.begin() + 2, frame.end());

    FrameParser parser;

    // Act & Assert (first feed):
    const auto first_results = parser.feed(chunk_1.data(), chunk_1.size());
    EXPECT_TRUE(first_results.empty());

    // Act & Assert (second feed):
    const auto second_results = parser.feed(chunk_2.data(), chunk_2.size());
    ASSERT_EQ(second_results.size(), 1u);
    EXPECT_EQ(second_results[0], expected_payload);
}

TEST(FrameParserTest, HandlesFragmentedPayload) {
    // Arrange:
    const auto expected_payload = payload_a();
    const auto frame = encode_frame(expected_payload);

    const std::vector<std::byte> chunk_1(frame.begin(), frame.begin() + 9);
    const std::vector<std::byte> chunk_2(frame.begin() + 9, frame.end());

    FrameParser parser;

    // Act & Assert (first feed):
    const auto first_results = parser.feed(chunk_1.data(), chunk_1.size());
    EXPECT_TRUE(first_results.empty());

    // Act & Assert (second feed):
    const auto second_results = parser.feed(chunk_2.data(), chunk_2.size());
    ASSERT_EQ(second_results.size(), 1u);
    EXPECT_EQ(second_results[0], expected_payload);
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
    const auto results = parser.feed(combined.data(), combined.size());

    // Assert:
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], expected_payload_a);
    EXPECT_EQ(results[1], expected_payload_b);
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
    const auto first_results = parser.feed(chunk_1.data(), chunk_1.size());

    ASSERT_EQ(first_results.size(), 2u);
    EXPECT_EQ(first_results[0], expected_payload_a);
    EXPECT_EQ(first_results[1], expected_payload_b);

    // Act & Assert (second feed):
    const auto second_results = parser.feed(chunk_2.data(), chunk_2.size());

    ASSERT_EQ(second_results.size(), 1u);
    EXPECT_EQ(second_results[0], expected_payload_c);
}

TEST(FrameParserTest, RejectsTooShortLength) {
    // Arrange:
    const std::vector<std::byte> prefix{std::byte{0x00}, std::byte{0x00},
                                        std::byte{0x00}, std::byte{0x0D}};

    FrameParser parser;

    // Act & Assert:
    EXPECT_THROW(parser.feed(prefix.data(), prefix.size()), std::runtime_error);
}

TEST(FrameParserTest, RejectsTooLongLength) {
    // Arrange:
    const std::vector<std::byte> prefix{std::byte{0x00}, std::byte{0x01},
                                        std::byte{0x00}, std::byte{0x0E}};

    FrameParser parser;

    // Act & Assert:
    EXPECT_THROW(parser.feed(prefix.data(), prefix.size()), std::runtime_error);
}

TEST(FrameParserTest, CleanEOF) {
    // Arrange:
    const auto expected_payload = payload_a();
    const auto frame = encode_frame(expected_payload);

    FrameParser parser;

    // Act:
    const auto results = parser.feed(frame.data(), frame.size());

    // Assert:
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], expected_payload);
    EXPECT_NO_THROW(parser.finish());
}

TEST(FrameParserTest, EOFWithPartialHeader) {
    // Arrange:
    const std::vector<std::byte> partial_header{std::byte{0x00},
                                                std::byte{0x00}};

    FrameParser parser;

    // Act:
    std::vector<std::vector<std::byte>> results;
    EXPECT_NO_THROW({
        results = parser.feed(partial_header.data(), partial_header.size());
    });

    // Assert:
    EXPECT_TRUE(results.empty());
    EXPECT_THROW(parser.finish(), std::runtime_error);
}

TEST(FrameParserTest, EOFWithPartialPayload) {
    // Arrange:
    const std::vector<std::byte> partial_frame{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x0E}, std::byte{0x01}, std::byte{0x02},
        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};

    FrameParser parser;

    // Act:
    std::vector<std::vector<std::byte>> results;
    EXPECT_NO_THROW(
        { results = parser.feed(partial_frame.data(), partial_frame.size()); });

    // Assert:
    EXPECT_TRUE(results.empty());
    EXPECT_THROW(parser.finish(), std::runtime_error);
}
