#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

void append_u32(std::vector<std::byte>& buffer, std::uint32_t value);

std::uint32_t read_u32(const std::vector<std::byte>& buffer,
                       std::size_t& offset);

void append_u16(std::vector<std::byte>& buffer, std::uint16_t value);

std::uint16_t read_u16(const std::vector<std::byte>& buffer,
                       std::size_t& offset);
