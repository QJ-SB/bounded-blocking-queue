#include "ingest_connection.h"

#include <stdexcept>
#include <utility>

#include "receive_once.h"
#include "trading_event_codec.h"

IngestResult
ingest_connection(int fd, BoundedBlockingQueue<TradingEventMessage>& queue) {

    // Initialize default return outcome:
    IngestResult outcome{IngestStatus::CleanEOF, std::nullopt, std::nullopt};

    // Ensure queue shutdown before propagating unexpected exceptions:
    try {

        FrameParser parser;  // one persistent parser per connection

        while (true) {
            ReceiveResult received = receive_once(fd, parser);

            /*
             Process completed payloads (decode & push to queue) BEFORE
             examining the terminal receive status:
             (Framing errors can COEXIT with completed data) */
            bool stop = false;
            for (const auto& payload : received.completed_payloads) {
                TradingEventMessage event;
                try {
                    event = decode_trading_event(payload);
                } catch (const std::out_of_range&) {  // known malformed-message
                                                      // condition
                    outcome.status = IngestStatus::MessageDecodeError;
                    stop = true;
                    break;
                }

                if (!queue.push(std::move(event))) {
                    // A consumer / The orchestrator has closed the queue:
                    outcome.status = IngestStatus::QueueClosed;
                    stop = true;
                    break;
                }
            }
            // Check bool FOR for(){}:
            if (stop) {
                break;
            }

            /*
             Examine receive status:
             ("Data" is the only non-terminal status)
             (all other statuses end ingestion) */
            if (received.status == ReceiveStatus::Data) {  // normal (continue)
                continue;
            } else if (received.status ==
                       ReceiveStatus::CleanEOF) {  // normal (end)
                outcome.status = IngestStatus::CleanEOF;
            } else if (received.status ==
                       ReceiveStatus::ProtocolError) {  // error (end)
                outcome.status = IngestStatus::FramingError;
                outcome.frame_error = received.protocol_error;
            } else {  // error (end)
                outcome.status = IngestStatus::TransportError;
                outcome.transport_error = received.transport_error;
            }

            // Exit the receive loop after recording the terminal status:
            break;
        }

    } catch (...) {
        /*
        - Unexpected exceptions must not leave the consumer sleeping in pop().
        - A failure inside close() itself is outside this minimal contract. */
        queue.close();  // wake up all sleeping consumer
        throw;          // Exception Masking may occur here
    }

    /*
    - This is the sole producer: after it stops, no future pushes are legal.
    - The consumer can drain remaining items, then pop() returns nullopt. */
    queue.close();
    return outcome;
}
