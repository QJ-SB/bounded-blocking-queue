#include "frame_parser.h"

#include <stdexcept>
#include <utility>

#include "binary_codec.h"
#include "frame_protocol.h"

FeedResult FrameParser::feed(const std::byte* data, std::size_t size) {
    // Check parser state:
    if (state_ != State::Active) {
        throw std::logic_error("FrameParser is not active");
    }

    // Setup:
    FeedResult result{};

    // Append the incoming bytes to persistent storage:
    if (size != 0) {
        pending_bytes_.insert(pending_bytes_.end(), data, data + size);
    }

    // Extract all currently available complete frames:
    while (true) {
        // Check complete 4-byte prefix (payload length):
        if (pending_bytes_.size() < frame_protocol::kLengthPrefixSize) {
            break;
        }

        // Decode prefix & Validate the payload length:
        std::size_t offset = 0;
        auto payload_length = read_u32(pending_bytes_, offset);
        if (payload_length < frame_protocol::kMinPayloadSize ||
            payload_length > frame_protocol::kMaxPayloadSize) {
            state_ = State::Failed;
            result.protocol_error = FrameError::InvalidLength;
            return result;
        }

        // Check if the entire frame is available:
        if (pending_bytes_.size() < offset + payload_length) {
            break;
        }

        // Extract the complete payload:
        std::vector<std::byte> payload(pending_bytes_.begin() + offset,
                                       pending_bytes_.begin() + offset +
                                           payload_length);
        result.completed_payloads.push_back(std::move(payload));

        // Consume the entire frame (including its prefix):
        pending_bytes_.erase(pending_bytes_.begin(),
                             pending_bytes_.begin() + offset + payload_length);

        // Continue...
    }

    return result;
}

std::optional<FrameError> FrameParser::finish() {
    // Inactive finsh:
    if (state_ != State::Active) {
        throw std::logic_error("FrameParser is not active");
    }

    // Truncated finish:
    if (!pending_bytes_.empty()) {
        state_ = State::Failed;
        return FrameError::TruncatedFrame;
    }

    // Normal finish:
    state_ = State::Finished;
    return std::nullopt;
}
