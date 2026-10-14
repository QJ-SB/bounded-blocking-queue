#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "binary_codec.h"


TEST(CodecTest, AppendU32ProducesKnownWireBytes) {
    // Setup:
    constexpr std::uint32_t value = 0x01020304;
    constexpr std::size_t byte_count = sizeof(std::uint32_t);
    std::vector<std::byte> buffer{};

    // Append:
    append_u32(buffer, value);

    // Verify (network byte order):
    ASSERT_EQ(buffer.size(), byte_count);  // avoid UB
    EXPECT_EQ(buffer[0], std::byte{0x01});
    EXPECT_EQ(buffer[1], std::byte{0x02});
    EXPECT_EQ(buffer[2], std::byte{0x03});
    EXPECT_EQ(buffer[3], std::byte{0x04});
}

TEST(CodecTest, ReadU32DecodesAndAdvancesOffset) {
    // Setup (network byte order):
    std::vector<std::byte> buffer{std::byte{0x01}, std::byte{0x02},
                                  std::byte{0x03}, std::byte{0x04}};
    std::size_t offset{0};

    // Read:
    auto ret = read_u32(buffer, offset);

    // Verify:
    EXPECT_EQ(ret, 0x01020304);
    EXPECT_EQ(offset, sizeof(std::uint32_t));
}

TEST(CodecTest, ReadU32RejectsInsufficientBytes) {
    // Setup:
    std::vector<std::byte> buffer{std::byte{0x01}, std::byte{0x02},
                                  std::byte{0x03}, std::byte{0x04},
                                  std::byte{0x05}};
    std::size_t offset{4};

    // Read & Verify ( buffer.size() - offset < sizeof(uint32_t) ):
    EXPECT_THROW(read_u32(buffer, offset), std::out_of_range);
    EXPECT_EQ(offset, 4);
}

TEST(CodecTest, ReadU32RejectsInvalidOffset) {
    // Setup:
    std::vector<std::byte> buffer{
        std::byte{0x01},
        std::byte{0x02},
    };
    std::size_t offset{4};

    // Read & Verify ( buffer.size() < offset ):
    EXPECT_THROW(read_u32(buffer, offset), std::out_of_range);
    EXPECT_EQ(offset, 4);
}

TEST(CodecTest, AppendThenReadU32RoundTrip) {
    // Setup:
    constexpr std::uint32_t value_1 = 0x01020304;
    constexpr std::uint32_t value_2 = 0x05060708;
    constexpr std::size_t byte_count = sizeof(std::uint32_t);
    std::vector<std::byte> buffer{};
    std::size_t offset{0};

    // Append:
    append_u32(buffer, value_1);
    append_u32(buffer, value_2);

    // Read & Verify:
    auto ret_1 = read_u32(buffer, offset);
    EXPECT_EQ(ret_1, value_1);
    EXPECT_EQ(offset, byte_count);
    auto ret_2 = read_u32(buffer, offset);
    EXPECT_EQ(ret_2, value_2);
    EXPECT_EQ(offset, byte_count * 2);
}
