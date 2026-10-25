#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <optional>
#include <sys/socket.h>
#include <system_error>
#include <utility>
#include <vector>

#include "frame_parser.h"

enum class ReceiveStatus { Data, CleanEOF, ProtocolError, TransportError };

struct ReceiveResult {
    std::vector<std::vector<std::byte>> completed_payloads;

    ReceiveStatus status = ReceiveStatus::Data;

    std::optional<FrameError> protocol_error;
    std::optional<std::error_code> transport_error;
};


namespace receive_once_detail {  // dependency injection

template <typename RecvOperation>
ReceiveResult receive_once_impl(int fd, FrameParser& parser,
                                RecvOperation recv_operation) {
    constexpr std::size_t kReceiveBufferSize = 4096;  // receive buffer
    std::array<std::byte, kReceiveBufferSize> buffer{};

    while (true) {
        const ssize_t n = recv_operation(fd, buffer.data(), buffer.size(), 0);

        if (n > 0) {  // receiving
            FeedResult parsed =
                parser.feed(buffer.data(), static_cast<std::size_t>(n));

            ReceiveResult result{};
            result.completed_payloads = std::move(parsed.completed_payloads);

            // Check if FrameError::InvalidLength from parser.feed():
            if (parsed.protocol_error.has_value()) {
                result.status = ReceiveStatus::ProtocolError;
                result.protocol_error = parsed.protocol_error;
            }

            return result;
        }

        if (n == 0) {  // EOF
            const std::optional<FrameError> error = parser.finish();

            ReceiveResult result{};

            // Check if FrameError::TruncatedFrame from parser.finish():
            if (error.has_value()) {
                result.status = ReceiveStatus::ProtocolError;
                result.protocol_error = error;
            } else {
                result.status = ReceiveStatus::CleanEOF;
            }

            return result;
        }

        // if n < 0, record errno:
        const int error = errno;

        if (error == EINTR) {  // interrupted by signal, retry
            continue;
        }

        ReceiveResult result{};
        result.status = ReceiveStatus::TransportError;
        result.transport_error =
            std::error_code(error, std::generic_category());

        return result;
    }
}

}  // namespace receive_once_detail


// Public production interface:
inline ReceiveResult receive_once(int fd, FrameParser& parser) {
    return receive_once_detail::receive_once_impl(fd, parser, &::recv);
}
