#pragma once

#include <cstddef>
#include <optional>
#include <vector>

// Shared public error contract:
enum class FrameError {
    InvalidLength,  // FrameParser::feed()
    TruncatedFrame  // FrameParser::finish()
};

struct FeedResult {
    std::vector<std::vector<std::byte>> completed_payloads;
    std::optional<FrameError> protocol_error;
};


class FrameParser {
public:
    FeedResult feed(const std::byte* data, std::size_t size);

    std::optional<FrameError> finish();

private:
    // Parser state:
    enum class State { Active, Finished, Failed };

    State state_ = State::Active;

    // Bytes received but not yet consumed:
    std::vector<std::byte> pending_bytes_;
};
