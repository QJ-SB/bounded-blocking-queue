#pragma once

#include <cerrno>
#include <cstddef>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>

namespace send_all_detail {

template <typename SendOperation>  // dependency injection
void send_all_impl(int fd, const std::byte* data, std::size_t size,
                   SendOperation send_operation) {
    // Empty data is a no-op:
    if (size == 0) {
        return;
    }

    // Check if valid ptr:
    if (data == nullptr) {
        throw std::invalid_argument("data is null with nonzero size");
    }

    std::size_t sent = 0;  // initialize sent

    while (sent < size) {
        const ssize_t n =
            send_operation(fd, data + sent, size - sent, MSG_NOSIGNAL);

        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }

        if (n == 0) {
            throw std::runtime_error("send made no progress");
        }

        // n < 0, record errno:
        const int error = errno;
        if (error == EINTR) {  // interrupted by signal, retry
            continue;
        }
        throw std::system_error(error, std::generic_category(), "send failed");
    }
}

}  // namespace send_all_detail


// Public production interface:
inline void send_all(int fd, const std::byte* data, std::size_t size) {
    send_all_detail::send_all_impl(fd, data, size, &::send);
}
