#pragma once

#include <optional>
#include <system_error>

#include "BoundedBlockingQueue.h"
#include "TradingEvent.h"
#include "frame_parser.h"

/*
A terminal outcome for one blocking TCP connection. Framing errors here,
correspond to ReceiveStatus::ProtocolError in receive_once(). */
enum class IngestStatus {
    CleanEOF,
    FramingError,        // frame_parser
    MessageDecodeError,  // decode_trading_event()
    TransportError,      // receiver socket
    QueueClosed          // bounded_blocking_queue & multi thread
};

struct IngestResult {
    IngestStatus status;
    std::optional<FrameError> frame_error;
    std::optional<std::error_code> transport_error;
};

/*
 - Borrows fd and queue; owns neither. (owns frame_parser per connection)
 - This function is synchronous, not a thread creator.
 - Closes queue on every normal/known-error return, and attempts to close it
   before propagating unexpected exceptions. */
IngestResult
ingest_connection(int fd, BoundedBlockingQueue<TradingEventMessage>& queue);
