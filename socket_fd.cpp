#include "socket_fd.h"

#include <unistd.h>
#include <utility>

SocketFd::SocketFd(int fd) noexcept : fd_(fd) {}

SocketFd::~SocketFd() noexcept {
    if (fd_ != -1) {
        (void)::close(fd_);  // currently ignore possible error
    }
}

SocketFd::SocketFd(SocketFd&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}  // move the fd & reset the other fd

SocketFd& SocketFd::operator=(SocketFd&& other) noexcept {
    // Self-assignment check:
    if (this == &other) {
        return *this;
    }

    if (fd_ != -1) {
        (void)::close(fd_);  // currently ignore possible error of close()
    }

    // Move the fd & Reset the other fd:
    fd_ = std::exchange(other.fd_, -1);

    return *this;
}

int SocketFd::get() const noexcept {
    return fd_;
}

bool SocketFd::valid() const noexcept {
    return fd_ != -1;
}
