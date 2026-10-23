#include <array>
#include <cerrno>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <type_traits>
#include <utility>

#include "socket_fd.h"

namespace {

// ============================================================
// Compile-time API Contract
// ============================================================

static_assert(!std::is_copy_constructible_v<SocketFd>);
static_assert(!std::is_copy_assignable_v<SocketFd>);

static_assert(std::is_nothrow_move_constructible_v<SocketFd>);
static_assert(std::is_nothrow_move_assignable_v<SocketFd>);
static_assert(std::is_nothrow_destructible_v<SocketFd>);


// ============================================================
// Test Helpers
// ============================================================

// Create real socket pair & Return ownership directly through SocketFd:
std::array<SocketFd, 2> make_socket_pair() {
    int fds[2] = {-1, -1};

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        const int error = errno;
        throw std::system_error(error, std::generic_category(),
                                "socketpair failed");
    }

    return {SocketFd(fds[0]), SocketFd(fds[1])};
}


// Verify that a kernel fd is still open:
void expect_fd_open(int fd) {
    const int result = ::fcntl(fd, F_GETFD);

    EXPECT_NE(result, -1);
}


// Verify that a kernel fd has been closed:
void expect_fd_closed(int fd) {
    const int result = ::fcntl(fd, F_GETFD);
    const int error = errno;  // keep errno after fcntl()

    EXPECT_EQ(result, -1);

    if (result == -1) {
        EXPECT_EQ(error, EBADF);
    }
}

}  // namespace


// ============================================================
// Test 1 — Default Construction
// ============================================================
TEST(SocketFdTest, DefaultConstructsEmpty) {
    // Arrange & Act:
    SocketFd socket{};

    // Asseert:
    EXPECT_FALSE(socket.valid());
    EXPECT_EQ(socket.get(), -1);
}


// ============================================================
// Test 2 — Destructor Releases Kernel Resource
// ============================================================
TEST(SocketFdTest, DestructorClosesOwnedFd) {
    // Arrange:
    int first_fd = -1;
    int second_fd = -1;

    // Act & Assert:
    {
        auto sockets = make_socket_pair();
        first_fd = sockets[0].get();
        second_fd = sockets[1].get();

        EXPECT_TRUE(sockets[0].valid());
        EXPECT_TRUE(sockets[1].valid());

        expect_fd_open(first_fd);
        expect_fd_open(second_fd);
    }  // manually create RAII environment

    // Assert:
    expect_fd_closed(first_fd);
    expect_fd_closed(second_fd);
}


// ============================================================
// Test 3 — Move Construction
// ============================================================
TEST(SocketFdTest, MoveConstructorTransfersOwnership) {
    // Arrange:
    auto sockets = make_socket_pair();
    const int original_fd = sockets[0].get();

    // Act & Assert:
    {
        SocketFd destination(std::move(sockets[0]));  // transfer
        EXPECT_FALSE(sockets[0].valid());
        EXPECT_EQ(sockets[0].get(), -1);

        EXPECT_TRUE(destination.valid());
        EXPECT_EQ(destination.get(), original_fd);

        expect_fd_open(
            original_fd);  // move must NOT close the transferred resource.
    }

    // Assert:
    expect_fd_closed(original_fd);
    expect_fd_open(sockets[1].get());
}


// ============================================================
// Test 4 — Move Assignment
// ============================================================
TEST(SocketFdTest, MoveAssignmentClosesOldFd) {
    // Arrange:
    auto source_pair = make_socket_pair();
    auto destination_pair = make_socket_pair();
    const int source_fd = source_pair[0].get();
    const int old_destination_fd = destination_pair[0].get();

    // Act:
    destination_pair[0] = std::move(source_pair[0]);

    // Assert:
    EXPECT_FALSE(source_pair[0].valid());
    EXPECT_EQ(source_pair[0].get(), -1);
    EXPECT_TRUE(destination_pair[0].valid());
    EXPECT_EQ(destination_pair[0].get(), source_fd);

    expect_fd_closed(old_destination_fd);
    expect_fd_open(source_fd);
}


// ============================================================
// Test 5 — Self Move Assignment
// ============================================================
TEST(SocketFdTest, SelfMoveAssignmentPreservesOwnership) {
    // Arrange:
    auto sockets = make_socket_pair();
    const int original_fd = sockets[0].get();
    SocketFd& alias = sockets[0];

    // Act:
    sockets[0] = std::move(alias);

    // Assert (self-move must NOT make any changes):
    EXPECT_TRUE(sockets[0].valid());
    EXPECT_EQ(sockets[0].get(), original_fd);
    expect_fd_open(original_fd);
}


// ============================================================
// Test 6 — Move Assignment from Empty Owner
// ============================================================
TEST(SocketFdTest, MoveFromEmptyReleasesDestination) {
    // Arrange:
    SocketFd empty{};
    auto sockets = make_socket_pair();
    const int old_fd = sockets[0].get();

    // Act:
    sockets[0] = std::move(empty);

    // Assert:
    EXPECT_FALSE(empty.valid());
    EXPECT_FALSE(sockets[0].valid());
    EXPECT_EQ(sockets[0].get(), -1);

    expect_fd_closed(
        old_fd);  // destination's previous resource must be released
    expect_fd_open(sockets[1].get());  // unrelated socket remains open
}


// ============================================================
// Test 7 — Stack Unwinding
// ============================================================
TEST(SocketFdTest, ClosesOwnedFdWhenExceptionExitsScope) {
    // Arrange:
    auto sockets = make_socket_pair();
    const int original_fd = sockets[0].get();

    // Act & Assert:
    EXPECT_THROW(
        [&]() {
            SocketFd owner(std::move(sockets[0]));

            expect_fd_open(original_fd);

            throw std::runtime_error("Trigger stack unwinding");
        }(),
        std::runtime_error);

    expect_fd_closed(
        original_fd);  // owner was destroyed during stack unwinding
    EXPECT_FALSE(sockets[0].valid());
    expect_fd_open(sockets[1].get());
}
