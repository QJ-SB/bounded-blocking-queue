#include "binary_codec.h"

#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// ============================= Append_* ===============================

void append_u32(std::vector<std::byte>& buffer, std::uint32_t value) {
    // Record offset:
    auto old_size = buffer.size();

    // Convert to network byte order:
    auto network_value = htonl(value);

    // Resize for new bytes (uint32_t):
    buffer.resize(old_size + sizeof(network_value));

    // Memcpy to external buffer (Append):
    std::memcpy(buffer.data() + old_size, &network_value,
                sizeof(network_value));
}

void append_u16(std::vector<std::byte>& buffer, std::uint16_t value) {
    // Record offset:
    auto old_size = buffer.size();

    // Convert to network byte order:
    auto network_value = htons(value);

    // Resize for new bytes (uint16_t):
    buffer.resize(old_size + sizeof(network_value));

    // Memcpy to external buffer (Append):
    std::memcpy(buffer.data() + old_size, &network_value,
                sizeof(network_value));
}


// ============================= Read_* ===============================

std::uint32_t read_u32(const std::vector<std::byte>& buffer,
                       std::size_t& offset) {

    constexpr std::size_t needed = sizeof(std::uint32_t);  // uint32_t length

    // Validate - Overflow / Insufficient (Short-circuit Evaluation):
    if (offset > buffer.size() || (buffer.size() - offset) < needed) {
        throw std::out_of_range("Insufficient bytes to read");
    }

    // Memcpy bytes (uint32_t) & Update offset (post-increment):
    std::uint32_t network_value{};
    std::memcpy(&network_value, buffer.data() + offset, needed);
    offset += needed;

    // Convert to host byte order & Return:
    return ntohl(network_value);
}

std::uint16_t read_u16(const std::vector<std::byte>& buffer,
                       std::size_t& offset) {

    constexpr std::size_t needed = sizeof(std::uint16_t);  // uint16_t length

    // Validate - Overflow / Insufficient (Short-circuit Evaluation):
    if (offset > buffer.size() || (buffer.size() - offset) < needed) {
        throw std::out_of_range("Insufficient Bytes to read");
    }

    // Memcpy bytes (uint16_t) & Update offset (post-increment):
    std::uint16_t network_value{};
    std::memcpy(&network_value, buffer.data() + offset, needed);
    offset += needed;

    // Convert to host byte order & Return:
    return ntohs(network_value);
}
