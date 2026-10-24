#pragma once

class SocketFd {
public:
    SocketFd() noexcept = default;
    explicit SocketFd(int fd) noexcept;
    ~SocketFd() noexcept;

    // Copy:
    SocketFd(const SocketFd&) = delete;
    SocketFd& operator=(const SocketFd&) = delete;

    // Move:
    SocketFd(SocketFd&& other) noexcept;
    SocketFd& operator=(SocketFd&& other) noexcept;

    int get() const noexcept;     // get the fd value
    bool valid() const noexcept;  // check if the fd is valid

private:
    int fd_ = -1;
};
