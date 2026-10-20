#include <cstddef>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "frame_encoder.h"
#include "frame_protocol.h"

TEST(FrameEncoderTest, EncodesValidPayload) {
    // Arrange:
    const std::vector<std::byte> payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
        std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C},
        std::byte{0x0D}, std::byte{0x0E}, std::byte{0x0F}, std::byte{0x10},
        std::byte{0x11}, std::byte{0x12}, std::byte{0x13}, std::byte{0x14},
    };

    const std::vector<std::byte> expected_prefix{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x14}};

    // Act:
    const auto frame = encode_frame(payload);

    // Assert:
    ASSERT_EQ(frame.size(), frame_protocol::kLengthPrefixSize + 20);

    const std::vector<std::byte> actual_prefix(
        frame.begin(), frame.begin() + frame_protocol::kLengthPrefixSize);
    EXPECT_EQ(actual_prefix, expected_prefix);

    const std::vector<std::byte> actual_payload(
        frame.begin() + frame_protocol::kLengthPrefixSize, frame.end());
    EXPECT_EQ(actual_payload, payload);
}

TEST(FrameEncoderTest, RejectsTooShortPayload) {
    // Arrange:
    const std::vector<std::byte> payload(frame_protocol::kMinPayloadSize - 1,
                                         std::byte{0x01});

    // Act & Assert:
    EXPECT_THROW(encode_frame(payload), std::length_error);
}

TEST(FrameEncoderTest, RejectsTooLongPayload) {
    // Arrange:
    const std::vector<std::byte> payload(frame_protocol::kMaxPayloadSize + 1,
                                         std::byte{0x01});

    // Act & Assert:
    EXPECT_THROW(encode_frame(payload), std::length_error);
}

TEST(FrameEncoderTest, AcceptsMinimumPayload) {
    // Arrange:
    const std::vector<std::byte> payload(frame_protocol::kMinPayloadSize,
                                         std::byte{0xAB});

    const std::vector<std::byte> expected_prefix{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0E}};

    // Act:
    const auto frame = encode_frame(payload);

    // Assert:
    ASSERT_EQ(frame.size(), frame_protocol::kLengthPrefixSize +
                                frame_protocol::kMinPayloadSize);

    const std::vector<std::byte> actual_prefix(
        frame.begin(), frame.begin() + frame_protocol::kLengthPrefixSize);
    EXPECT_EQ(actual_prefix, expected_prefix);

    const std::vector<std::byte> actual_payload(
        frame.begin() + frame_protocol::kLengthPrefixSize, frame.end());
    EXPECT_EQ(actual_payload, payload);
}

TEST(FrameEncoderTest, AcceptsMaximumPayload) {
    // Arrange:
    const std::vector<std::byte> payload(frame_protocol::kMaxPayloadSize,
                                         std::byte{0xAB});

    const std::vector<std::byte> expected_prefix{
        std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x0D}};

    // Act:
    const auto frame = encode_frame(payload);

    // Assert:
    ASSERT_EQ(frame.size(), frame_protocol::kLengthPrefixSize +
                                frame_protocol::kMaxPayloadSize);

    const std::vector<std::byte> actual_prefix(
        frame.begin(), frame.begin() + frame_protocol::kLengthPrefixSize);
    EXPECT_EQ(actual_prefix, expected_prefix);

    const std::vector<std::byte> actual_payload(
        frame.begin() + frame_protocol::kLengthPrefixSize, frame.end());
    EXPECT_EQ(actual_payload, payload);
}
