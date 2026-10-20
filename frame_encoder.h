#pragma once

#include <cstddef>
#include <vector>

std::vector<std::byte> encode_frame(const std::vector<std::byte>& payload);
