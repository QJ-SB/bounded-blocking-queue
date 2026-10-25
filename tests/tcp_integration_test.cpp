#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <utility>
#include <vector>

#include "TradingEvent.h"
#include "frame_encoder.h"
#include "receive_once.h"
#include "send_all.h"
#include "socket_fd.h"
#include "trading_event_codec.h"

namespace {

/// =================================================================
/// Test Helpers
/// =================================================================

// ================== Real sockets test helper ======================

[[noreturn]] void throw_socket_error(const char* operation) {
    const int error = errno;
    throw std::system_error(error, std::generic_category(), operation);
}

struct LoopbackTcpSockets {  // RAII owns fds
    SocketFd listener;
    SocketFd client;
    SocketFd accepted;
};

LoopbackTcpSockets make_tcp_loopback_connection() {
    // 1. Create a listening TCP socket, owned immediately by RAII:
    const int listen_raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_raw == -1) {
        throw_socket_error("socket(listener) failed");
    }
    SocketFd listener(listen_raw);

    // 2. Bind to 127.0.0.1, with an ephemeral port chosen by the OS:
    sockaddr_in listen_address{};
    listen_address.sin_family = AF_INET;
    listen_address.sin_port = ::htons(0);
    if (::inet_pton(AF_INET, "127.0.0.1", &listen_address.sin_addr) != 1) {
        throw std::runtime_error("invalid loopback IPv4 address");
    }

    if (::bind(listener.get(),
               reinterpret_cast<const sockaddr*>(&listen_address),
               sizeof(listen_address)) == -1) {
        throw_socket_error("bind failed");
    }

    // 3. Obtain the OS-assigned port:
    sockaddr_in bound_address{};
    socklen_t bound_length = sizeof(bound_address);
    if (::getsockname(listener.get(),
                      reinterpret_cast<sockaddr*>(&bound_address),
                      &bound_length) == -1) {
        throw_socket_error("getsockname failed");
    }

    // 4. Start listening before attempting to connect.
    if (::listen(listener.get(), 1) == -1) {
        throw_socket_error("listen failed");
    }

    // 5. Connect a client to the listening address.
    const int client_raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client_raw == -1) {
        throw_socket_error("socket(client) failed");
    }
    SocketFd client(client_raw);

    if (::connect(client.get(),
                  reinterpret_cast<const sockaddr*>(&bound_address),
                  bound_length) == -1) {
        throw_socket_error("connect failed");
    }

    // 6. Accept the established connection: this is a *new* owned FD.
    const int accepted_raw = ::accept(listener.get(), nullptr, nullptr);
    if (accepted_raw == -1) {
        throw_socket_error("accept failed");
    }
    SocketFd accepted(accepted_raw);

    return {std::move(listener), std::move(client), std::move(accepted)};
}

void set_socket_timeouts(int fd) {
    const timeval timeout{2, 0};
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ==
        -1) {
        throw_socket_error("setsockopt(SO_RCVTIMEO) failed");
    }
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ==
        -1) {
        throw_socket_error("setsockopt(SO_SNDTIMEO) failed");
    }
}


// ================= GoogleTest assertion helper ====================

void expect_event_equals(const TradingEventMessage& actual,
                         const TradingEventMessage& expected) {
    EXPECT_EQ(actual.type, expected.type);
    EXPECT_EQ(actual.price_ticks, expected.price_ticks);
    EXPECT_EQ(actual.quantity, expected.quantity);
    EXPECT_EQ(actual.symbol, expected.symbol);
}

}  // namespace


// ====================================================================
// TCP Success Path — Multiple Valid frame, valid TradingEvent payload
// ====================================================================

TEST(TcpIntegrationTest, TransfersMultipleTradingEvents) {
    // ARRANGE: real IPv4 TCP endpoints and a known message sequence.
    auto sockets = make_tcp_loopback_connection();
    ASSERT_TRUE(sockets.listener.valid());
    ASSERT_TRUE(sockets.client.valid());
    ASSERT_TRUE(sockets.accepted.valid());

    set_socket_timeouts(sockets.client.get());
    set_socket_timeouts(sockets.accepted.get());

    const std::vector<TradingEventMessage> expected{
        {1u, 100250u, 12u, "AAPL"},
        {2u, 98765u, 7u, "MSFT"},
        {3u, 314159u, 1u, ""},
    };

    // ACT: encode each event, frame it, and send it over real TCP.
    std::size_t sent_wire_bytes = 0;
    for (const auto& event : expected) {
        const auto payload = encode_trading_event(event);
        const auto frame = encode_frame(payload);
        sent_wire_bytes += frame.size();
        send_all(sockets.client.get(), frame.data(), frame.size());
    }

    // Half-close the writing direction, so the receiver eventually sees EOF.
    ASSERT_EQ(::shutdown(sockets.client.get(), SHUT_WR), 0);

    // ACT: one persistent FrameParser across multiple receive_once() calls.
    FrameParser parser;
    std::vector<TradingEventMessage> received;
    bool saw_clean_eof = false;

    // Each Data result consumes at least one byte, plus one final EOF call.
    // This is an upper bound, NOT an assumption about TCP segmentation.
    for (std::size_t attempt = 0; attempt <= sent_wire_bytes; ++attempt) {
        ReceiveResult result = receive_once(sockets.accepted.get(), parser);

        // First deliver all completed payloads:
        // (also important for error cases)
        for (const auto& payload : result.completed_payloads) {
            received.push_back(decode_trading_event(payload));
        }

        // The happy path must have no transport/framing errors.
        ASSERT_FALSE(result.protocol_error.has_value());
        ASSERT_FALSE(result.transport_error.has_value());

        if (result.status == ReceiveStatus::CleanEOF) {  // make sure EOF
            saw_clean_eof = true;
            break;
        }

        ASSERT_EQ(result.status, ReceiveStatus::Data);
    }

    // ASSERT: complete, ordered, field-by-field end-to-end reconstruction.
    ASSERT_TRUE(saw_clean_eof);
    ASSERT_EQ(received.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE(::testing::Message() << "TradingEvent index = " << i);
        expect_event_equals(received[i], expected[i]);
    }

    // The receiver did not take ownership of the SocketFd.
    EXPECT_TRUE(sockets.accepted.valid());
    EXPECT_NE(::fcntl(sockets.accepted.get(), F_GETFD), -1);
}

// =============================================================
// TCP Failure Path #1 — Partial frame followed by orderly EOF
// =============================================================

TEST(TcpIntegrationTest, ReportsTruncatedFrameAtEOF) {
    // ARRANGE: a real connected IPv4/TCP pair.
    auto sockets = make_tcp_loopback_connection();
    set_socket_timeouts(sockets.client.get());
    set_socket_timeouts(sockets.accepted.get());

    const TradingEventMessage event{1u, 100250u, 12u, "AAPL"};
    const auto frame = encode_frame(encode_trading_event(event));
    constexpr std::size_t kPartialHeaderBytes = 2;
    ASSERT_GT(frame.size(), kPartialHeaderBytes);

    // ACT: send only half of the 4-byte frame length prefix, then FIN.
    send_all(sockets.client.get(), frame.data(), kPartialHeaderBytes);
    ASSERT_EQ(::shutdown(sockets.client.get(), SHUT_WR), 0);

    FrameParser parser;
    bool saw_truncated_error = false;

    // At most 2 nonempty recv calls, plus one EOF call. Do not assert
    // that the first recv returns both bytes together.
    for (std::size_t attempt = 0; attempt <= kPartialHeaderBytes; ++attempt) {
        const ReceiveResult result =
            receive_once(sockets.accepted.get(), parser);

        EXPECT_TRUE(result.completed_payloads.empty());
        EXPECT_FALSE(result.transport_error.has_value());

        if (result.status == ReceiveStatus::ProtocolError) {
            ASSERT_TRUE(result.protocol_error.has_value());
            EXPECT_EQ(*result.protocol_error, FrameError::TruncatedFrame);
            saw_truncated_error = true;
            break;
        }

        ASSERT_EQ(result.status, ReceiveStatus::Data);
        EXPECT_FALSE(result.protocol_error.has_value());
    }

    EXPECT_TRUE(saw_truncated_error);
    EXPECT_TRUE(sockets.accepted.valid());
    EXPECT_NE(::fcntl(sockets.accepted.get(), F_GETFD), -1);
}

// =================================================================
// TCP Failure Path #2 — Deliver valid events before a framing error
// =================================================================

TEST(TcpIntegrationTest, PreservesEventsBeforeFramingFailure) {
    // ARRANGE: [valid A frame][invalid zero-length frame prefix].
    auto sockets = make_tcp_loopback_connection();
    set_socket_timeouts(sockets.client.get());
    set_socket_timeouts(sockets.accepted.get());

    const TradingEventMessage event_a{1u, 100250u, 12u, "AAPL"};
    std::vector<std::byte> wire = encode_frame(encode_trading_event(event_a));

    // A 4-byte big-endian length of zero violates kMinPayloadSize = 14.
    wire.insert(wire.end(), 4, std::byte{0x00});

    send_all(sockets.client.get(), wire.data(), wire.size());
    ASSERT_EQ(::shutdown(sockets.client.get(), SHUT_WR), 0);

    FrameParser parser;
    std::vector<TradingEventMessage> delivered;
    bool saw_invalid_length = false;

    // TCP can split A and the bad header arbitrarily. In a single recv,
    // FrameParser may return {completed A, InvalidLength} together.
    for (std::size_t attempt = 0; attempt <= wire.size(); ++attempt) {
        const ReceiveResult result =
            receive_once(sockets.accepted.get(), parser);

        // Crucial ordering: deliver completed frames BEFORE inspecting status.
        for (const auto& payload : result.completed_payloads) {
            delivered.push_back(decode_trading_event(payload));
        }

        EXPECT_FALSE(result.transport_error.has_value());

        if (result.status == ReceiveStatus::ProtocolError) {
            ASSERT_TRUE(result.protocol_error.has_value());
            EXPECT_EQ(*result.protocol_error, FrameError::InvalidLength);
            saw_invalid_length = true;
            break;
        }

        ASSERT_EQ(result.status, ReceiveStatus::Data);
        EXPECT_FALSE(result.protocol_error.has_value());
    }

    ASSERT_TRUE(saw_invalid_length);
    ASSERT_EQ(delivered.size(), 1u);
    expect_event_equals(delivered[0], event_a);
}

// ================================================================
// TCP Failure Path #3 — Valid frame, invalid TradingEvent payload
// ================================================================

TEST(TcpIntegrationTest, TestLoopStopsDeliveringAfterTradingEventDecodeError) {
    // ARRANGE: [valid A][framing-valid but semantically invalid B][valid C].
    auto sockets = make_tcp_loopback_connection();
    set_socket_timeouts(sockets.client.get());
    set_socket_timeouts(sockets.accepted.get());

    const TradingEventMessage event_a{1u, 100250u, 12u, "AAPL"};
    const TradingEventMessage event_b{2u, 98765u, 7u, "BAD"};
    const TradingEventMessage event_c{3u, 314159u, 1u, "MSFT"};

    auto invalid_payload_b = encode_trading_event(event_b);

    // TradingEvent payload layout (not the outer frame header):
    // [type:4][price_ticks:4][quantity:4][symbol_length:2][symbol:N]
    // "BAD" has N=3, but deliberately declare N=4 (big-endian).
    constexpr std::size_t kSymbolLengthOffset =
        12;  // 12 = 4(type) + 4(price_ticks) + 4(quantity)
    ASSERT_EQ(invalid_payload_b.size(),
              17u);  // 14 fixed bytes + 3 symbol bytes
    invalid_payload_b[kSymbolLengthOffset] = std::byte{0x00};
    invalid_payload_b[kSymbolLengthOffset + 1] = std::byte{0x04};

    // encode_frame() is deliberately given the malformed *message* payload:
    // its outer frame length remains valid, so FrameParser will accept B.
    std::vector<std::byte> wire = encode_frame(encode_trading_event(event_a));
    const auto frame_b = encode_frame(invalid_payload_b);
    const auto frame_c = encode_frame(encode_trading_event(event_c));
    wire.insert(wire.end(), frame_b.begin(), frame_b.end());
    wire.insert(wire.end(), frame_c.begin(), frame_c.end());

    send_all(sockets.client.get(), wire.data(), wire.size());
    ASSERT_EQ(::shutdown(sockets.client.get(), SHUT_WR), 0);

    FrameParser parser;
    std::vector<TradingEventMessage> delivered;
    bool saw_symbol_length_mismatch = false;

    for (std::size_t attempt = 0; attempt <= wire.size(); ++attempt) {
        const ReceiveResult result =
            receive_once(sockets.accepted.get(), parser);

        // The frame layer should stay successful; B fails only in the decoder.
        ASSERT_EQ(result.status, ReceiveStatus::Data);
        ASSERT_FALSE(result.protocol_error.has_value());
        ASSERT_FALSE(result.transport_error.has_value());

        for (const auto& payload : result.completed_payloads) {
            try {
                delivered.push_back(decode_trading_event(payload));
            } catch (const std::out_of_range& error) {
                // Specific semantic error, not a transport/framing failure.
                EXPECT_STREQ(error.what(), "mismatched symbol_length");
                saw_symbol_length_mismatch = true;
                break;
            }
        }

        // Fail-fast per connection:
        // （do NOT process C, and do NOT call recv again）
        if (saw_symbol_length_mismatch) {
            break;
        }
    }

    ASSERT_TRUE(saw_symbol_length_mismatch);
    ASSERT_EQ(delivered.size(), 1u);
    expect_event_equals(delivered[0], event_a);
    EXPECT_TRUE(sockets.accepted.valid());
    EXPECT_NE(::fcntl(sockets.accepted.get(), F_GETFD), -1);
}
