# Trading Event Pipeline

A C++17 systems-engineering project built around a bounded blocking queue and extended into a **single-connection TCP trading-event ingestion path**. It combines binary codecs, length-prefixed framing, incremental parsing, socket ownership, and concurrent producer/consumer handoff.

**Scope:** tested Linux/POSIX building blocks and localhost integration—not a production market-data feed handler.

## Two execution paths

The repository currently contains **two distinct examples**, which should not be confused:

1. `events_pipeline` (from `main.cpp`) is an **in-memory** one-producer/one-consumer queue demo. It checks event count, FIFO sequence, and an aggregate payload sum while reporting a simple end-to-end throughput measurement.
2. `tests/tcp_integration_test.cpp` exercises the **real localhost TCP → framed message → queue → consumer** integration. The synchronous `ingest_connection()` API is a component under test; the `events_pipeline` executable is **not** a TCP server.

### Networked ingestion architecture

```text
Synthetic TradingEventMessage
             |
     encode_trading_event()
             |
        encode_frame()
             |
         send_all()
             |
     TCP (127.0.0.1)
             |
     receive_once(fd, parser)    <-- persistent FrameParser per connection
             |
        completed payloads
             |
     decode_trading_event()
             |
        ingest_connection()
             |
 BoundedBlockingQueue<TradingEventMessage>
             |
        Consumer worker
```

The networking functions **borrow** the file descriptor; `SocketFd` is a move-only RAII owner. `ingest_connection()` borrows its queue, acts as its sole producer in this integration, and closes the queue when ingestion terminates so the consumer can drain accepted events.

## Components and contracts

| Component | Contract |
| --- | --- |
| `BoundedBlockingQueue.h` | Bounded FIFO, blocking `push`/`pop`, `close()` wakes waiters, existing items remain drainable. |
| `binary_codec.*` | Encode/decode 16-bit and 32-bit unsigned integers in network byte order. |
| `trading_event_codec.*` | Convert `TradingEventMessage` to/from a binary payload with a variable-size symbol. |
| `frame_encoder.*`, `frame_protocol.h` | Prefix each payload with a four-byte, big-endian payload length. |
| `frame_parser.*` | Incrementally reconstruct zero or more complete payloads from arbitrary TCP chunks; report invalid lengths or truncation. |
| `socket_fd.*` | Unique, move-only ownership of a POSIX file descriptor. |
| `send_all.h` | Account for partial sends, retry `EINTR`, and avoid `SIGPIPE` via Linux `MSG_NOSIGNAL`. |
| `receive_once.h` | Perform one blocking receive step, feed the persistent parser, and distinguish data, orderly EOF, framing errors, and transport errors. |
| `ingest_connection.*` | Decode completed payloads, push events to the queue, and return a terminal ingestion outcome. |

### Wire format

All fixed-width integers are encoded in **big-endian** order.

```text
TCP frame:
+-------------------------+--------------------------------------+
| payload_length (u32)    | TradingEventMessage payload bytes    |
+-------------------------+--------------------------------------+

TradingEventMessage payload:
+------------+------------------+----------------+------------------+----------------+
| type (u32) | price_ticks (u32)| quantity (u32) | symbol_len (u16) | symbol bytes   |
+------------+------------------+----------------+------------------+----------------+
```

The valid payload length is **14–65,549 bytes** (14 fixed bytes plus up to 65,535 symbol bytes). TCP is treated as a byte stream: a `recv()` chunk is **not** assumed to contain exactly one frame.

### Failure and lifecycle semantics

- A `FrameParser::feed()` call may return **completed payloads alongside a later framing error**. Accepted complete payloads are processed first.
- Orderly socket EOF invokes `FrameParser::finish()`; remaining partial-frame bytes yield `TruncatedFrame`.
- A transport error is reported separately; it is not silently reclassified as clean EOF or protocol truncation.
- Invalid decoded messages terminate the current ingestion path rather than attempting to resynchronize.
- `ingest_connection()` reports `CleanEOF`, `FramingError`, `MessageDecodeError`, `TransportError`, or `QueueClosed`, and closes the queue on its termination path.
- A closed queue rejects later pushes but allows the consumer to drain already accepted items.

## Build, run, and test

**Requirements:** Linux/POSIX socket APIs, a C++17 compiler, CMake 3.14+, and a build tool. Tests use GoogleTest v1.14.0 via CMake `FetchContent`, requiring network access on the first configuration if it is not cached.

```bash
git clone https://github.com/QJ-SB/trading-event-pipeline.git
cd trading-event-pipeline

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

./build/events_pipeline

# Run all tests (from the build directory)
cd build
ctest --output-on-failure
```

The demo produces synthetic `TradingEvent` objects (distinct from the wire-level `TradingEventMessage`) and measures the in-memory queue pipeline. Its throughput is a local demonstration measurement, **not** a TCP or production latency benchmark.

### Sanitizer configurations

ASan + UBSan:

```bash
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN_UBSAN=ON
cmake --build build-san -j
cd build-san
ctest --output-on-failure
```

ThreadSanitizer (run in a separate build directory and a compatible Linux toolchain/runtime):

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON
cmake --build build-tsan -j
cd build-tsan
ctest --output-on-failure
```

`ENABLE_ASAN_UBSAN` and `ENABLE_TSAN` are intentionally mutually exclusive. In the AS-02 local Ubuntu verification reported on **2026-10-29**, the full suite passed **83/83** under each of Normal, ASan+UBSan, and TSan. These are **local reported results**, not an independently reproduced GitHub CI status.

## Verification coverage

The `tests/` directory covers:

- Queue FIFO behavior, full/empty blocking, close wakeups, draining, and multiple producers/consumers.
- Integer codecs, payload serialization, frame boundaries, fragmented headers/payloads, multiple frames, and invalid/truncated frames.
- `SocketFd` move/destruction behavior and injected send/receive failures (including partial writes and `EINTR`).
- Real TCP loopback success and failures, plus network-to-queue integration with ordered event delivery and failure-before-drain behavior.

Tests focus on bounded correctness and failure contracts; they do **not** establish production-scale load handling or a deterministic full-queue backpressure event at the integrated TCP-to-queue level.

## Repository map

```text
.
├── main.cpp                      # In-memory queue demo
├── TradingEvent.h                # Demo event and wire message types
├── BoundedBlockingQueue.h        # Header-only concurrent queue
├── binary_codec.*                # Network-order integers
├── trading_event_codec.*         # Message serialization
├── frame_protocol.h
├── frame_encoder.*
├── frame_parser.*                # Stateful incremental reassembly
├── socket_fd.*                   # Move-only FD owner
├── send_all.h
├── receive_once.h                # Blocking I/O helpers
├── ingest_connection.*           # Network → queue integration
├── tests/                        # Unit and localhost integration tests
└── CMakeLists.txt
```

## Limitations

The current ingestion design assumes a single blocking TCP connection and a sole queue producer. It has no nonblocking event loop, `epoll` server, multiple-client orchestration, reconnect/recovery layer, active cancellation for a blocked `recv()`, exchange-specific protocol, TLS, or persistent production deployment. Throughput/latency distributions, profiler-driven tuning, and production observability are outside the demonstrated scope.

This repository is a verifiable learning and engineering work surface, not a production-ready trading system.
