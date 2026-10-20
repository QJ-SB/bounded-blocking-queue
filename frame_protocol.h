#pragma once

#include <cstddef>

namespace frame_protocol {

inline constexpr std::size_t kLengthPrefixSize = 4;
inline constexpr std::size_t kMinPayloadSize = 14;
inline constexpr std::size_t kMaxPayloadSize = 14 + 65535;

}  // namespace frame_protocol
