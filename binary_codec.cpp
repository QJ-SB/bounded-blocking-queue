#include "binary_codec.h"

#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>


void append_u32(std::vector<std::byte>& buffer, std::uint32_t value) {
    auto old_size = buffer.size();

    auto network_value = htonl(value);

    buffer.resize(old_size + sizeof(network_value));

    std::memcpy(buffer.data() + old_size, &network_value,
                sizeof(network_value));
}

void append_u16(std::vector<std::byte>& buffer, std::uint16_t value) {
    auto old_size = buffer.size();

    auto network_value = htons(value);

    buffer.resize(old_size + sizeof(network_value));

    std::memcpy(buffer.data() + old_size, &network_value,
                sizeof(network_value));
}

std::uint32_t read_u32(const std::vector<std::byte>& buffer,
                       std::size_t& offset) {
    constexpr std::size_t needed = sizeof(std::uint32_t);

    if (offset > buffer.size() || buffer.size() - offset < needed) {
        throw std::out_of_range("Insufficient bytes");
    }

    std::uint32_t network_value{};

    std::memcpy(&network_value, buffer.data() + offset, needed);
    offset += needed;

    return ntohl(network_value);
}

std::uint16_t read_u16(const std::vector<std::byte>& buffer,
                       std::size_t& offset) {
    constexpr std::size_t needed = sizeof(std::uint16_t);

    if (buffer.size() < offset || buffer.size() - offset < needed) {
        throw std::out_of_range("Insufficient Bytes");
    }

    std::uint16_t network_value{};

    std::memcpy(&network_value, buffer.data() + offset, needed);
    offset += needed;

    return ntohs(network_value);
}
