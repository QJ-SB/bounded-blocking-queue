#include "frame_parser.h"

#include <stdexcept>
#include <utility>

#include "binary_codec.h"
#include "frame_protocol.h"

std::vector<std::vector<std::byte>> FrameParser::feed(const std::byte* data,
                                                      std::size_t size) {
    std::vector<std::vector<std::byte>> completed_payloads;

    // Append the incoming bytes to persistent storage.
    if (size != 0) {
        pending_bytes_.insert(pending_bytes_.end(), data, data + size);
    }

    // Extract all currently available complete frames.
    while (true) {
        // Wait for a complete 4-byte length prefix.
        if (pending_bytes_.size() < 4) {
            break;
        }

        // Decode and validate the payload length.
        std::size_t offset = 0;
        auto payload_length = read_u32(pending_bytes_, offset);

        if (payload_length < frame_protocol::kMinPayloadSize ||
            payload_length > frame_protocol::kMaxPayloadSize) {
            throw std::runtime_error("Invalid payload length");
        }

        // Wait until the entire frame is available.
        if (pending_bytes_.size() < offset + payload_length) {
            break;
        }

        // Extract the complete payload.
        std::vector<std::byte> payload(pending_bytes_.begin() + offset,
                                       pending_bytes_.begin() + offset +
                                           payload_length);

        completed_payloads.push_back(std::move(payload));

        // Consume the entire frame, including its prefix.
        pending_bytes_.erase(pending_bytes_.begin(),
                             pending_bytes_.begin() + offset + payload_length);
    }

    return completed_payloads;
}

void FrameParser::finish() {
    if (!pending_bytes_.empty()) {
        throw std::runtime_error("Truncated frame at EOF");
    }
}
