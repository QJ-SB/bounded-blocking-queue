#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

void append_u32(std::vector<std::byte>& buffer, std::uint32_t value);

std::uint32_t read_u32(const std::vector<std::byte>& buffer,
                       std::size_t& offset);
