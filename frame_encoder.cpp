#include "frame_encoder.h"

#include <cstdint>
#include <stdexcept>

#include "binary_codec.h"
#include "frame_protocol.h"

std::vector<std::byte> encode_frame(const std::vector<std::byte>& payload) {
    // Validate the payload length:
    if (payload.size() < frame_protocol::kMinPayloadSize ||
        payload.size() > frame_protocol::kMaxPayloadSize) {
        throw std::length_error("Invalid payload length");
    }

    // Setup:
    std::vector<std::byte> frame;

    // Concatenate prefix and payload:
    append_u32(frame, static_cast<std::uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());

    return frame;
}
