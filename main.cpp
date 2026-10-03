#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

#include "BoundedBlockingQueue.h"
#include "TradingEvent.h"

constexpr std::size_t kQueueCapacity = 4;
constexpr std::uint64_t kEventCount = 50'000;

constexpr std::int64_t kExpectedPayloadSum =
    static_cast<std::int64_t>((kEventCount * (kEventCount + 1)) / 2);

struct ConsumerResult {
    std::vector<std::uint64_t> sequences;
    std::int64_t payload_sum{0};
};

int main() {
    // Setup:
    BoundedBlockingQueue<TradingEvent> queue{kQueueCapacity};
    ConsumerResult consumer_result;  // Consumer-side observable result.
    bool producer_success{true};     // Whether all intended pushes succeeded.


    // Start time measurement:
    auto t_start = std::chrono::steady_clock::now();


    // Consumer thread:
    std::thread consumer{[&queue, &consumer_result] {
        while (true) {
            auto item = queue.pop();

            if (!item.has_value()) {
                return;
            }

            const auto& event = item.value();
            consumer_result.sequences.push_back(event.sequence);
            consumer_result.payload_sum += event.payload;
        }
    }};

    // Producer thread:
    std::thread producer{[&queue, &producer_success] {
        for (std::uint64_t sequence = 0; sequence < kEventCount; ++sequence) {
            TradingEvent event{sequence, static_cast<int>(sequence + 1)};

            const bool pushed = queue.push(std::move(event));

            if (!pushed) {
                producer_success = false;
                return;
            }
        }
    }};

    // Lifecycle coordination:
    producer.join();  // No future production is possible after this point.

    if (!producer_success) {
        // Give the consumer a deterministic termination path.
        queue.close();
        consumer.join();

        std::cerr << "Producer unexpectedly failed to push all events.\n";
        return EXIT_FAILURE;
    }

    // Transition from production to closed/draining.
    queue.close();

    // Wait until the consumer has drained the queue and terminated.
    consumer.join();


    // End time measurement:
    auto t_end = std::chrono::steady_clock::now();

    // Calculate measurement result:
    const std::chrono::duration<double> elapsed = t_end - t_start;
    std::cout << "Time spent: " << elapsed.count() << " s\n";

    const double throughput = kEventCount / elapsed.count();
    std::cout << "Throughput: " << throughput << " events/s\n";

    // Final verification: count.
    if (consumer_result.sequences.size() != kEventCount) {
        std::cerr << "Unexpected number of consumed events.\n";
        return EXIT_FAILURE;
    }

    // Final verification: identity and FIFO ordering.
    for (std::uint64_t i = 0; i < kEventCount; ++i) {
        if (consumer_result.sequences[i] != i) {
            std::cerr
                << "Consumer sequence does not match producer sequence.\n";
            return EXIT_FAILURE;
        }
    }

    // Final verification: deterministic payload processing.
    if (consumer_result.payload_sum != kExpectedPayloadSum) {
        std::cerr << "Consumer payload sum is incorrect.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Trading event pipeline verification passed.\n";
    return EXIT_SUCCESS;
}
