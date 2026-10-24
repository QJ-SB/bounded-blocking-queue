#include <array>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <functional>
#include <gtest/gtest.h>
#include <initializer_list>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <system_error>
#include <utility>
#include <vector>

#include "send_all.h"
#include "socket_fd.h"

namespace {

// ============================================================
// Test Helpers
// ============================================================
constexpr int kFakeFd = 123;

std::array<std::byte, 10> sample_payload() {
    return {
        std::byte{0x41}, std::byte{0x42}, std::byte{0x43}, std::byte{0x44},
        std::byte{0x45}, std::byte{0x46}, std::byte{0x47}, std::byte{0x48},
        std::byte{0x49}, std::byte{0x4A},
    };
}

struct SendStep {  // represents a scripted return
    ssize_t result;
    int error = 0;
};

struct SendCall {  // represents a call to send
    int fd;
    const void* ptr;
    std::size_t length;
    int flags;
};

class ScriptedSend {  // callable object that simulates send() calls
public:
    explicit ScriptedSend(std::initializer_list<SendStep> steps)
        : script_(steps) {}

    ssize_t operator()(int fd, const void* ptr, std::size_t length, int flags) {
        // Index of the next call:
        const std::size_t index = calls.size();

        // Check if the next call overflows:
        if (index >= script_.size()) {
            throw std::logic_error("Unexpected extra send operation");
        }

        // Record the call:
        calls.push_back({fd, ptr, length, flags});

        // Return the scripted result:
        const SendStep& step = script_[index];

        if (step.result == -1) {
            errno = step.error;
        }

        return step.result;
    }

    std::vector<SendCall> calls;  // record of calls made

private:
    std::vector<SendStep> script_;  // scripted return values
};

void expect_call(const SendCall& call, int expected_fd,
                 const std::byte* expected_ptr,
                 std::size_t expected_length) {  // checks that if a SendCall
                                                 // matches the expected values
    EXPECT_EQ(call.fd, expected_fd);
    EXPECT_EQ(call.ptr, static_cast<const void*>(expected_ptr));
    EXPECT_EQ(call.length, expected_length);
    EXPECT_EQ(call.flags, MSG_NOSIGNAL);
}

std::array<SocketFd, 2> make_socket_pair() {  // makes a real socket pair
    int fds[2] = {-1, -1};

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        const int error = errno;

        throw std::system_error(error, std::generic_category(),
                                "socketpair failed");
    }

    return {SocketFd(fds[0]), SocketFd(fds[1])};  // SocketFd takes ownership
}

}  // namespace


// ============================================================
// Group A — Fake Sends
// ============================================================
TEST(SendAllTest, AcceptsEmptyBuffer) {  // Zero-byte Send = No-op
    // Arrange:
    ScriptedSend fake{{10, 0}};

    // Act & Assert:
    EXPECT_NO_THROW(
        send_all_detail::send_all_impl(kFakeFd, nullptr, 0, std::ref(fake)));
    EXPECT_TRUE(fake.calls.empty());
}

TEST(SendAllTest, RejectsNullDataWithNonzeroSize) {
    // Arrange:
    ScriptedSend fake{{10, 0}};

    // Act & Assert:
    EXPECT_THROW(
        send_all_detail::send_all_impl(kFakeFd, nullptr, 10, std::ref(fake)),
        std::invalid_argument);
    EXPECT_TRUE(fake.calls.empty());
}

TEST(SendAllTest, SendsAllBytesInOneCall) {
    // Arrange:
    const auto payload = sample_payload();
    ScriptedSend fake{{10, 0}};

    // Act & Assert:
    ASSERT_NO_THROW(send_all_detail::send_all_impl(
        kFakeFd, payload.data(), payload.size(), std::ref(fake)));
    ASSERT_EQ(fake.calls.size(), 1u);
    expect_call(fake.calls[0], kFakeFd, payload.data(), 10);
}

TEST(SendAllTest, HandlesPartialWrites) {
    // Arrange:
    const auto payload = sample_payload();
    ScriptedSend fake{
        {4, 0},
        {2, 0},
        {4, 0},
    };

    // Act & Assert:
    ASSERT_NO_THROW(send_all_detail::send_all_impl(
        kFakeFd, payload.data(), payload.size(), std::ref(fake)));
    ASSERT_EQ(fake.calls.size(), 3u);
    expect_call(fake.calls[0], kFakeFd, payload.data(), 10);
    expect_call(fake.calls[1], kFakeFd, payload.data() + 4, 6);
    expect_call(fake.calls[2], kFakeFd, payload.data() + 6, 4);
}

TEST(SendAllTest, RetriesInterruptedSend) {
    // Arrange:
    const auto payload = sample_payload();
    ScriptedSend fake{
        {4, 0},
        {-1, EINTR},
        {6, 0},
    };

    ASSERT_NO_THROW(send_all_detail::send_all_impl(
        kFakeFd, payload.data(), payload.size(), std::ref(fake)));
    ASSERT_EQ(fake.calls.size(), 3u);  // actually occurred 3 calls
    expect_call(fake.calls[0], kFakeFd, payload.data(), 10);
    expect_call(fake.calls[1], kFakeFd, payload.data() + 4, 6);
    expect_call(fake.calls[2], kFakeFd, payload.data() + 4, 6);
}

TEST(SendAllTest, RejectsNoProgress) {
    // Arrange:
    const auto payload = sample_payload();
    ScriptedSend fake{
        {4, 0},
        {0, 0},
    };

    // Act & Assert:
    try {
        send_all_detail::send_all_impl(kFakeFd, payload.data(), payload.size(),
                                       std::ref(fake));

        FAIL() << "Expected a no-progress exception";
    } catch (const std::system_error&) {
        FAIL() << "No progress must not use stale errno";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "send made no progress");
    }

    ASSERT_EQ(fake.calls.size(), 2u);
    expect_call(fake.calls[0], kFakeFd, payload.data(), 10);
    expect_call(fake.calls[1], kFakeFd, payload.data() + 4, 6);
}

TEST(SendAllTest, PropagatesSystemError) {
    // Arrange:
    const auto payload = sample_payload();
    ScriptedSend fake{
        {-1, EBADF},
    };

    // Act & Assert:
    try {
        send_all_detail::send_all_impl(kFakeFd, payload.data(), payload.size(),
                                       std::ref(fake));

        FAIL() << "Expected std::system_error";
    } catch (const std::system_error& e) {
        EXPECT_EQ(e.code(), std::error_code(EBADF, std::generic_category()));
    }

    ASSERT_EQ(fake.calls.size(), 1u);
    expect_call(fake.calls[0], kFakeFd, payload.data(), 10);
}

TEST(SendAllTest, ReportsFailureAfterPartialProgress) {
    // Arrange:
    const auto payload = sample_payload();
    ScriptedSend fake{
        {4, 0},
        {-1, EPIPE},
    };

    // Act & Assert:
    try {
        send_all_detail::send_all_impl(kFakeFd, payload.data(), payload.size(),
                                       std::ref(fake));

        FAIL() << "Expected std::system_error";
    } catch (const std::system_error& e) {
        EXPECT_EQ(e.code(), std::error_code(EPIPE, std::generic_category()));
    }

    ASSERT_EQ(fake.calls.size(), 2u);
    expect_call(fake.calls[0], kFakeFd, payload.data(), 10);
    expect_call(fake.calls[1], kFakeFd, payload.data() + 4, 6);
}


// ============================================================
// Group B — Real Sockets
// ============================================================
TEST(SendAllTest, SendsBytesThroughSocketPair) {
    // Arrange:
    auto sockets = make_socket_pair();
    const int sender_fd = sockets[0].get();
    const int receiver_fd = sockets[1].get();
    const timeval timeout{2, 0};
    ASSERT_EQ(::setsockopt(sender_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                           sizeof(timeout)),
              0);
    ASSERT_EQ(::setsockopt(receiver_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                           sizeof(timeout)),
              0);
    const auto payload = sample_payload();

    // Act & Assert:
    // Sender:
    ASSERT_NO_THROW(send_all(sender_fd, payload.data(), payload.size()));
    ASSERT_EQ(::shutdown(sender_fd, SHUT_WR), 0);

    // Reciver:
    std::array<std::byte, 10> received{};
    std::size_t total_received = 0;

    while (total_received < received.size()) {
        const ssize_t n = ::recv(receiver_fd, received.data() + total_received,
                                 received.size() - total_received, 0);

        const int error = errno;  // if fail, record errno instantly
        ASSERT_GT(n, 0) << "recv failed or ended early, errno=" << error;
        total_received += static_cast<std::size_t>(n);
    }
    EXPECT_EQ(received, payload);

    std::byte extra{};
    EXPECT_EQ(::recv(receiver_fd, &extra, 1, 0), 0);

    EXPECT_TRUE(sockets[0].valid());
    EXPECT_NE(::fcntl(sender_fd, F_GETFD), -1);
}

TEST(SendAllTest, ReportsBrokenPipeWithoutTermination) {
    // Arrange:
    auto sockets = make_socket_pair();
    const int sender_fd = sockets[0].get();
    const auto payload = sample_payload();
    sockets[1] = SocketFd{};  // keep SocketFd's ownership
    EXPECT_FALSE(sockets[1].valid());

    // Act & Assert:
    try {
        send_all(sender_fd, payload.data(), payload.size());

        FAIL() << "Expected EPIPE system_error";
    } catch (const std::system_error& e) {
        EXPECT_EQ(e.code(), std::error_code(EPIPE, std::generic_category()));
    }

    EXPECT_TRUE(sockets[0].valid());
    EXPECT_NE(::fcntl(sender_fd, F_GETFD), -1);
}
