#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <gtest/gtest.h>
#include <initializer_list>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <utility>
#include <vector>

#include "frame_encoder.h"
#include "receive_once.h"
#include "send_all.h"
#include "socket_fd.h"

namespace {

/// =================================================================
/// Test Helpers
/// =================================================================

// =========================== Mock/Fake ============================

constexpr int kFakeFd = 123;

std::vector<std::byte> payload_a() {  // 14 bytes
    return {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
            std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
            std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C},
            std::byte{0x0D}, std::byte{0x0E}};
}

std::vector<std::byte> payload_b() {  // 16 bytes
    return {std::byte{0x21}, std::byte{0x22}, std::byte{0x23}, std::byte{0x24},
            std::byte{0x25}, std::byte{0x26}, std::byte{0x27}, std::byte{0x28},
            std::byte{0x29}, std::byte{0x2A}, std::byte{0x2B}, std::byte{0x2C},
            std::byte{0x2D}, std::byte{0x2E}, std::byte{0x2F}, std::byte{0x30}};
}

std::vector<std::byte> slice(const std::vector<std::byte>& bytes,
                             std::size_t begin, std::size_t end) {
    return {bytes.begin() + begin, bytes.begin() + end};  // frame slicer
}

enum class RecvKind { Data, Eof, Error };  // receive status

struct RecvStep {  // represents scripted aggregate return
    RecvKind kind;
    std::vector<std::byte> bytes;
    int error = 0;

    static RecvStep data(std::vector<std::byte> bytes) {
        return {RecvKind::Data, std::move(bytes), 0};
    }
    static RecvStep eof() {
        return {RecvKind::Eof, {}, 0};
    }
    static RecvStep fail(int error) {
        return {RecvKind::Error, {}, error};
    }
};

struct RecvCall {  // represents a call to receive_once()
    int fd;
    std::size_t capacity;
    int flags;
    bool buffer_nonnull;
};

class ScriptedRecv {  // callable object that simulates recv() calls
public:
    explicit ScriptedRecv(std::initializer_list<RecvStep> steps)
        : script_(steps) {}

    ssize_t operator()(int fd, void* buffer, std::size_t capacity, int flags) {
        // Check if overflows scripted calls:
        if (next_ >= script_.size()) {
            throw std::logic_error("Unexpected extra recv operation");
        }

        // Record the call:
        calls.push_back({fd, capacity, flags, buffer != nullptr});

        // Return the scripted result:
        const RecvStep& step = script_[next_++];

        if (step.kind == RecvKind::Error) {
            errno = step.error;
            return -1;
        }
        if (step.kind == RecvKind::Eof) {
            return 0;
        }

        // Check scripted data validity & Write them into receive buffer:
        if (buffer == nullptr || step.bytes.empty() ||
            step.bytes.size() > capacity) {
            throw std::logic_error("Invalid scripted recv data");
        }
        std::memcpy(buffer, step.bytes.data(), step.bytes.size());
        return static_cast<ssize_t>(step.bytes.size());
    }

    std::vector<RecvCall> calls;  // record of calls made

private:
    std::vector<RecvStep> script_;  // scripted return values
    std::size_t next_ = 0;          // index of the next call
};


// ================= GoogleTest assertion helper ====================

void expect_call(const RecvCall& call) {
    EXPECT_EQ(call.fd, kFakeFd);
    EXPECT_GT(call.capacity, 0u);
    EXPECT_EQ(call.flags, 0);
    EXPECT_TRUE(call.buffer_nonnull);
}

void expect_no_errors(const ReceiveResult& result) {
    EXPECT_FALSE(result.protocol_error.has_value());
    EXPECT_FALSE(result.transport_error.has_value());
}


// ================== Real sockets test helper ======================

std::array<SocketFd, 2> make_socket_pair() {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        const int error = errno;
        throw std::system_error(error, std::generic_category(),
                                "socketpair failed");
    }
    return {SocketFd(fds[0]), SocketFd(fds[1])};
}

void set_socket_timeouts(int fd) {
    const timeval timeout{2, 0};
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) !=
            0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) !=
            0) {
        const int error = errno;
        throw std::system_error(error, std::generic_category(),
                                "setsockopt failed");
    }
}

}  // namespace


// ============================================================
// Group A — Fake Recvs + Real FrameParser
// ============================================================

TEST(ReceiveOnceTest, ReceivesCompleteFrame) {
    // Arrange:
    const auto payload = payload_a();
    ScriptedRecv fake{RecvStep::data(encode_frame(payload))};
    FrameParser parser;

    // Act:
    const auto result =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(result.status, ReceiveStatus::Data);
    expect_no_errors(result);
    ASSERT_EQ(result.completed_payloads.size(), 1u);
    EXPECT_EQ(result.completed_payloads[0], payload);
    ASSERT_EQ(fake.calls.size(), 1u);
    expect_call(fake.calls[0]);
}

TEST(ReceiveOnceTest, HandlesFragmentedHeader) {
    // Arrange:
    const auto payload = payload_a();
    const auto frame = encode_frame(payload);
    ScriptedRecv fake{RecvStep::data(slice(frame, 0, 2)),
                      RecvStep::data(slice(frame, 2, frame.size()))};
    FrameParser parser;

    // Act:
    const auto first =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(first.status, ReceiveStatus::Data);
    expect_no_errors(first);
    EXPECT_TRUE(first.completed_payloads.empty());
    EXPECT_EQ(fake.calls.size(), 1u);  // Once means one successful recv.

    // Act:
    const auto second =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(second.status, ReceiveStatus::Data);
    expect_no_errors(second);
    ASSERT_EQ(second.completed_payloads.size(), 1u);
    EXPECT_EQ(second.completed_payloads[0], payload);
    ASSERT_EQ(fake.calls.size(), 2u);
    expect_call(fake.calls[0]);
    expect_call(fake.calls[1]);
}

TEST(ReceiveOnceTest, HandlesFragmentedPayload) {
    // Arrange:
    const auto payload = payload_a();
    const auto frame = encode_frame(payload);
    ScriptedRecv fake{RecvStep::data(slice(frame, 0, 9)),
                      RecvStep::data(slice(frame, 9, frame.size()))};
    FrameParser parser;

    // Act:
    const auto first =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(first.status, ReceiveStatus::Data);
    expect_no_errors(first);
    EXPECT_TRUE(first.completed_payloads.empty());

    // Act:
    const auto second =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(second.status, ReceiveStatus::Data);
    expect_no_errors(second);
    ASSERT_EQ(second.completed_payloads.size(), 1u);
    EXPECT_EQ(second.completed_payloads[0], payload);
    ASSERT_EQ(fake.calls.size(), 2u);
}

TEST(ReceiveOnceTest, ReceivesMultipleFrames) {
    // Arrange:
    const auto a = payload_a();
    const auto b = payload_b();
    auto wire = encode_frame(a);
    const auto frame_b = encode_frame(b);
    wire.insert(wire.end(), frame_b.begin(), frame_b.end());
    ScriptedRecv fake{RecvStep::data(std::move(wire))};
    FrameParser parser;

    // Act:
    const auto result =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(result.status, ReceiveStatus::Data);
    expect_no_errors(result);
    ASSERT_EQ(result.completed_payloads.size(), 2u);
    EXPECT_EQ(result.completed_payloads[0], a);
    EXPECT_EQ(result.completed_payloads[1], b);
    EXPECT_EQ(fake.calls.size(), 1u);
}

TEST(ReceiveOnceTest, PreservesPayloadBeforeProtocolError) {
    // Arrange:
    const auto payload = payload_a();
    auto wire = encode_frame(payload);
    wire.insert(
        wire.end(), 4,
        std::byte{
            0});  // the next four bytes declare an invalid zero-length payload
    ScriptedRecv fake{RecvStep::data(std::move(wire))};
    FrameParser parser;

    // Act:
    const auto result =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(result.status, ReceiveStatus::ProtocolError);
    ASSERT_TRUE(result.protocol_error.has_value());
    EXPECT_EQ(*result.protocol_error, FrameError::InvalidLength);
    EXPECT_FALSE(result.transport_error.has_value());
    ASSERT_EQ(result.completed_payloads.size(), 1u);
    EXPECT_EQ(result.completed_payloads[0], payload);
    EXPECT_EQ(fake.calls.size(), 1u);
}

TEST(ReceiveOnceTest, ReportsCleanEOF) {
    // Arrange:
    ScriptedRecv fake{RecvStep::eof()};
    FrameParser parser;

    // Act:
    const auto result =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(result.status, ReceiveStatus::CleanEOF);
    EXPECT_TRUE(result.completed_payloads.empty());
    expect_no_errors(result);
    EXPECT_EQ(fake.calls.size(), 1u);
}

TEST(ReceiveOnceTest, ReportsTruncatedFrameAtEOF) {
    // Arrange:
    const auto frame = encode_frame(payload_a());
    ScriptedRecv fake{RecvStep::data(slice(frame, 0, frame.size() - 1)),
                      RecvStep::eof()};
    FrameParser parser;

    // Act:
    const auto first =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(first.status, ReceiveStatus::Data);
    EXPECT_TRUE(first.completed_payloads.empty());
    expect_no_errors(first);

    // Act:
    const auto second =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(second.status, ReceiveStatus::ProtocolError);
    ASSERT_TRUE(second.protocol_error.has_value());
    EXPECT_EQ(*second.protocol_error, FrameError::TruncatedFrame);
    EXPECT_FALSE(second.transport_error.has_value());
    EXPECT_TRUE(second.completed_payloads.empty());
    EXPECT_EQ(fake.calls.size(), 2u);
}

TEST(ReceiveOnceTest, RetriesEINTRThenReceivesData) {
    // Arrange:
    const auto payload = payload_a();
    ScriptedRecv fake{RecvStep::fail(EINTR),
                      RecvStep::data(encode_frame(payload))};
    FrameParser parser;

    // Act:
    const auto result =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(result.status, ReceiveStatus::Data);
    expect_no_errors(result);
    ASSERT_EQ(result.completed_payloads.size(), 1u);
    EXPECT_EQ(result.completed_payloads[0], payload);
    ASSERT_EQ(fake.calls.size(), 2u);
    expect_call(fake.calls[0]);
    expect_call(fake.calls[1]);
}

TEST(ReceiveOnceTest, TransportErrorDoesNotFinalizeParser) {
    // Arrange:
    const auto frame = encode_frame(payload_a());
    ScriptedRecv fake{RecvStep::data(slice(frame, 0, 2)),
                      RecvStep::fail(ECONNRESET)};
    FrameParser parser;

    // Act:
    const auto first =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(first.status, ReceiveStatus::Data);
    EXPECT_TRUE(first.completed_payloads.empty());

    // Act:
    const auto second =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(second.status, ReceiveStatus::TransportError);
    EXPECT_TRUE(second.completed_payloads.empty());
    EXPECT_FALSE(second.protocol_error.has_value());
    ASSERT_TRUE(second.transport_error.has_value());
    EXPECT_EQ(*second.transport_error,
              std::error_code(ECONNRESET, std::generic_category()));
    EXPECT_EQ(fake.calls.size(), 2u);

    // Test-only inspection: receiver must not have called finish().
    // Act:
    const auto eof_check = parser.finish();
    // Assert:
    ASSERT_TRUE(eof_check.has_value());
    EXPECT_EQ(*eof_check, FrameError::TruncatedFrame);
}

TEST(ReceiveOnceTest, RetriesEINTRThenReportsTransportError) {
    // Arrange:
    ScriptedRecv fake{RecvStep::fail(EINTR), RecvStep::fail(EIO)};
    FrameParser parser;

    // Act:
    const auto result =
        receive_once_detail::receive_once_impl(kFakeFd, parser, std::ref(fake));

    // Assert:
    EXPECT_EQ(result.status, ReceiveStatus::TransportError);
    EXPECT_TRUE(result.completed_payloads.empty());
    EXPECT_FALSE(result.protocol_error.has_value());
    ASSERT_TRUE(result.transport_error.has_value());
    EXPECT_EQ(*result.transport_error,
              std::error_code(EIO, std::generic_category()));
    ASSERT_EQ(fake.calls.size(), 2u);
    expect_call(fake.calls[0]);
    expect_call(fake.calls[1]);
}

// ============================================================
// Group B — Real Sockets
// ============================================================

TEST(ReceiveOnceTest, ReceivesFramesThroughSocketPair) {
    // Arrange & Assert:
    auto sockets = make_socket_pair();
    ASSERT_NO_THROW(set_socket_timeouts(sockets[0].get()));
    ASSERT_NO_THROW(set_socket_timeouts(sockets[1].get()));
    const auto a = payload_a();
    const auto b = payload_b();
    const auto frame_a = encode_frame(a);
    const auto frame_b = encode_frame(b);

    // Act & Assert:
    ASSERT_NO_THROW(send_all(sockets[0].get(), frame_a.data(), frame_a.size()));
    ASSERT_NO_THROW(send_all(sockets[0].get(), frame_b.data(), frame_b.size()));
    ASSERT_EQ(::shutdown(sockets[0].get(), SHUT_WR), 0);

    FrameParser parser;
    std::vector<std::vector<std::byte>> received;
    bool saw_clean_eof = false;

    for (int i = 0; i < 128; ++i) {  // one recv() != one frame / one send_all()
        auto result = receive_once(sockets[1].get(), parser);
        EXPECT_FALSE(result.protocol_error.has_value());
        EXPECT_FALSE(result.transport_error.has_value());
        for (auto& payload : result.completed_payloads) {
            received.push_back(std::move(payload));
        }
        if (result.status == ReceiveStatus::CleanEOF) {
            saw_clean_eof = true;
            break;
        }
        ASSERT_EQ(result.status, ReceiveStatus::Data);
    }

    EXPECT_TRUE(saw_clean_eof);
    EXPECT_EQ(received, (std::vector<std::vector<std::byte>>{a, b}));
    EXPECT_TRUE(sockets[1].valid());
    EXPECT_NE(::fcntl(sockets[1].get(), F_GETFD), -1);
}

TEST(ReceiveOnceTest, DetectsTruncatedEOFThroughSocketPair) {
    // Arrange & Assert:
    auto sockets = make_socket_pair();
    ASSERT_NO_THROW(set_socket_timeouts(sockets[0].get()));
    ASSERT_NO_THROW(set_socket_timeouts(sockets[1].get()));
    const auto frame = encode_frame(payload_a());

    // Act & Assert:
    // Only send the first two bytes of the length prefix & Shutdown sneder:
    ASSERT_NO_THROW(send_all(sockets[0].get(), frame.data(), 2));
    ASSERT_EQ(::shutdown(sockets[0].get(), SHUT_WR), 0);

    FrameParser parser;
    bool saw_truncated_eof = false;

    for (int i = 0; i < 8; ++i) {  // one recv() != one frame / one send_all()
        const auto result = receive_once(sockets[1].get(), parser);

        if (result.status == ReceiveStatus::Data) {
            EXPECT_TRUE(result.completed_payloads.empty());
            expect_no_errors(result);
            continue;
        }

        ASSERT_EQ(result.status, ReceiveStatus::ProtocolError);
        ASSERT_TRUE(result.protocol_error.has_value());

        EXPECT_EQ(*result.protocol_error, FrameError::TruncatedFrame);

        EXPECT_FALSE(result.transport_error.has_value());
        EXPECT_TRUE(result.completed_payloads.empty());

        saw_truncated_eof = true;
        break;
    }

    EXPECT_TRUE(saw_truncated_eof);
    EXPECT_TRUE(sockets[1].valid());
}
