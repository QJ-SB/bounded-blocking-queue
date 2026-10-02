#include <algorithm>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "BoundedBlockingQueue.h"


//==============================================================================
// Queue test
//==============================================================================

TEST(QueueTest, PushThenPopReturnsSameValue) {
    BoundedBlockingQueue<int> q{5};

    ASSERT_TRUE(q.push(42));  // push

    auto item = q.pop();  // pop
    ASSERT_TRUE(item.has_value());

    EXPECT_EQ(item.value(), 42);
}

TEST(QueueTest, PreservesFIFOOrder) {
    BoundedBlockingQueue<char> q{5};

    ASSERT_TRUE(q.push('A'));  // push in order
    ASSERT_TRUE(q.push('B'));
    ASSERT_TRUE(q.push('C'));

    auto item_1 = q.pop();  // pop in order
    ASSERT_TRUE(item_1.has_value());
    EXPECT_EQ(item_1.value(), 'A');
    auto item_2 = q.pop();
    ASSERT_TRUE(item_2.has_value());
    EXPECT_EQ(item_2.value(), 'B');
    auto item_3 = q.pop();
    ASSERT_TRUE(item_3.has_value());
    EXPECT_EQ(item_3.value(), 'C');
}

TEST(QueueTest, PushAfterCloseReturnsFalse) {
    BoundedBlockingQueue<int> q{1};
    q.close();  // close queue

    EXPECT_FALSE(q.push(10));
}

TEST(QueueTest, CloseThenPopEmptyReturnsNullopt) {
    BoundedBlockingQueue<int> q{1};

    q.close();  // close queue

    EXPECT_EQ(q.pop(), std::nullopt);  // close && empty returns nullopt
}

TEST(QueueTest, CloseStillDrainsExistingItems) {
    BoundedBlockingQueue<char> q{3};
    ASSERT_TRUE(q.push('A'));
    ASSERT_TRUE(q.push('B'));

    q.close();  // close queue with items left


    EXPECT_EQ(q.pop(), 'A');  // start to drain
    EXPECT_EQ(q.pop(), 'B');
    EXPECT_EQ(q.pop(), std::nullopt);  // verify close && empty
}

TEST(QueueTest, CloseIsIdempotent) {
    BoundedBlockingQueue<char> q{3};
    ASSERT_TRUE(q.push('A'));
    ASSERT_TRUE(q.push('B'));
    q.close();

    q.close();  // double close

    EXPECT_FALSE(q.push('C'));  // verify still normal
    EXPECT_EQ(q.pop(), 'A');
    EXPECT_EQ(q.pop(), 'B');
    EXPECT_EQ(q.pop(), std::nullopt);
}

TEST(QueueTest, ZeroCapacityIsRejected) {
    EXPECT_THROW(BoundedBlockingQueue<int>{0}, std::invalid_argument);
}


//==============================================================================
// Thread test
//==============================================================================
TEST(QueueTest, PopBlocksUntilItemIsPushed) {
    // Setup:
    BoundedBlockingQueue<int> q{1};
    std::promise<void> started_promise;  // signal: about to call pop()
    std::future<void> started_future = started_promise.get_future();
    std::promise<std::optional<int>>
        result_promise;  // publishes the result after pop() returns
    std::future<std::optional<int>> result_future = result_promise.get_future();

    // Start consumer:
    std::thread consumer{[&q, &started_promise, &result_promise] {
        started_promise
            .set_value();  // Signal that the consumer is about to call pop()
        auto item = q.pop();
        result_promise.set_value(
            std::move(item));  // publish pop() completion and result
    }};

    // Verify pre-wait state:
    auto pre_wait_status =
        started_future.wait_for(std::chrono::milliseconds(100));
    if (pre_wait_status == std::future_status::timeout) {
        q.close();        // ensure consumer has a termination path!!!
        consumer.join();  // Cleanup: resolve consumer thread lifetime
        FAIL() << "consumer did not reach the pop() checkpoint";
    }

    // Verify pre-push state:
    auto pre_push_status =
        result_future.wait_for(std::chrono::milliseconds(100));
    if (pre_push_status == std::future_status::ready) {
        consumer.join();
        FAIL() << "pop() already happened before push(42)";
    }

    // Trigger push(42):
    const bool pushed = q.push(42);
    if (!pushed) {
        q.close();
        consumer.join();
        FAIL() << "push(42) unexpectedly failed";
    }

    // Wait for pop() completion (give more time):
    auto after_push_status = result_future.wait_for(std::chrono::seconds(1));

    // Verify post-push completion:
    if (after_push_status == std::future_status::timeout) {
        q.close();
        consumer.join();
        FAIL() << "pop() did not complete after push(42)";
    } else if (after_push_status == std::future_status::ready) {
        consumer.join();  // Cleanup: resolve consumer thread lifetime first
        auto result = result_future.get();
        ASSERT_TRUE(result.has_value());  // Verify result:
        EXPECT_EQ(result.value(),
                  42);  // Consumer successfully popped & returned
    }
}

TEST(QueueTest, PushBlocksUntilSpaceIsAvailable) {
    // Setup:
    BoundedBlockingQueue<int> q{1};
    ASSERT_TRUE(q.push(42));
    std::promise<void> started_promise;
    std::future<void> started_future = started_promise.get_future();
    std::promise<bool> result_promise;
    std::future<bool> result_future = result_promise.get_future();

    // Start producer:
    std::thread producer{[&q, &started_promise, &result_promise] {
        started_promise.set_value();
        const bool pushed = q.push(43);
        result_promise.set_value(pushed);
    }};

    // Verify pre-wait state:
    auto pre_wait_status =
        started_future.wait_for(std::chrono::milliseconds(100));
    if (pre_wait_status == std::future_status::timeout) {
        q.close();
        producer.join();
        FAIL() << "producer did not reach the push(43) checkpoint";
    }

    // Verify pre-pop state:
    auto pre_pop_status =
        result_future.wait_for(std::chrono::milliseconds(100));
    if (pre_pop_status == std::future_status::ready) {
        producer.join();
        FAIL() << "push(43) completed before pop() made space available";
    }

    // Trigger pop():
    auto item = q.pop();
    if (!item.has_value()) {
        q.close();
        producer.join();
        FAIL() << "pop() unexpectedly returned nullopt";
    }
    EXPECT_EQ(item.value(), 42);

    // Wait for push() completion (give more time):
    auto after_pop_status = result_future.wait_for(std::chrono::seconds(1));

    // Verify post-pop completion:
    if (after_pop_status == std::future_status::timeout) {
        q.close();
        producer.join();
        FAIL() << "push(43) did not complete after pop()";
    } else if (after_pop_status == std::future_status::ready) {
        producer.join();
        ASSERT_TRUE(result_future.get());

        // establish a bounded termination path for final verification:
        q.close();

        auto committed_item = q.pop();
        ASSERT_TRUE(committed_item.has_value());
        EXPECT_EQ(committed_item.value(), 43);
    }
}

TEST(QueueTest, CloseWakesBlockedConsumer) {
    // Setup:
    BoundedBlockingQueue<int> q{1};
    std::promise<void> started_promise;
    std::future<void> started_future = started_promise.get_future();
    std::promise<std::optional<int>> result_promise;
    std::future<std::optional<int>> result_future = result_promise.get_future();

    // Start consumer:
    std::thread consumer{[&q, &started_promise, &result_promise] {
        started_promise.set_value();
        auto item = q.pop();
        result_promise.set_value(std::move(item));
    }};

    // Verify pre-wait status:
    auto pre_wait_status =
        started_future.wait_for(std::chrono::milliseconds(100));
    if (pre_wait_status == std::future_status::timeout) {
        const bool pushed =
            q.push(43);  // substitute for close() to establish termination path
        EXPECT_TRUE(pushed);
        consumer.join();
        FAIL() << "consumer did not reach the pop() checkpoint";
    }

    // Verify pre_close status:
    auto pre_close_status =
        result_future.wait_for(std::chrono::milliseconds(100));
    if (pre_close_status == std::future_status::ready) {
        consumer.join();
        FAIL() << "pop() completed before close() transitioned the queue to "
                  "closed";
    }

    // Trigger close()
    q.close();

    // Wait for pop() completion after close (give more time):
    auto after_close_status = result_future.wait_for(std::chrono::seconds(1));

    // Verify post-close completion:
    if (after_close_status == std::future_status::timeout) {
        // No independent public-API rescue path remains after close().
        // This failure mode requires an external test timeout.
        consumer.join();
        FAIL() << "pop() did not complete after close()";
    } else if (after_close_status == std::future_status::ready) {
        consumer.join();
        auto ret = result_future.get();
        ASSERT_FALSE(ret.has_value());
    }
}

TEST(QueueTest, CloseWakesBlockedProducer) {
    // Setup:
    BoundedBlockingQueue<int> q{1};
    ASSERT_TRUE(q.push(42));
    std::promise<void> started_promise;
    std::future<void> started_future = started_promise.get_future();
    std::promise<bool> result_promise;
    std::future<bool> result_future = result_promise.get_future();

    // Start producer:
    std::thread producer{[&q, &started_promise, &result_promise] {
        started_promise.set_value();
        const bool pushed = q.push(43);
        result_promise.set_value(pushed);
    }};

    // Verify pre-wait status:
    auto pre_wait_status =
        started_future.wait_for(std::chrono::milliseconds(100));
    if (pre_wait_status == std::future_status::timeout) {
        auto popped =
            q.pop();  // substitute for close() to establish cleanup path
        EXPECT_TRUE(popped.has_value());
        if (popped.has_value()) {
            EXPECT_EQ(popped.value(), 42);
        }

        producer.join();
        FAIL() << "producer did not reach the push(43) checkpoint";
    }

    // Verify pre_close status:
    auto pre_close_status =
        result_future.wait_for(std::chrono::milliseconds(100));
    if (pre_close_status == std::future_status::ready) {
        producer.join();
        FAIL() << "push(43) completed before close() turn queue to terminate";
    }

    // Trigger close()
    q.close();

    // Wait for push(43) completion after close (give more time):
    auto after_close_status = result_future.wait_for(std::chrono::seconds(1));

    // Verify post-close completion:
    if (after_close_status == std::future_status::timeout) {
        auto cleanup_item =
            q.pop();  // substitute for close() to establish cleanup path
        EXPECT_TRUE(cleanup_item.has_value());
        if (cleanup_item.has_value()) {
            EXPECT_EQ(cleanup_item.value(), 42);
        }

        producer.join();
        FAIL() << "push(43) did not complete after close()";
    } else if (after_close_status == std::future_status::ready) {
        producer.join();
        ASSERT_FALSE(result_future.get());
    }
}

TEST(QueueTest, MultiProducerMultiConsumerPreservesAllItems) {
    // Setup:
    constexpr int kItemsPerProducer = 1000;
    constexpr int kProducerCount = 2;
    constexpr int kTotalItems = kItemsPerProducer * kProducerCount;
    BoundedBlockingQueue<int> q{4};

    // Producer-local outcome (observable flag):
    bool producer_0_success = true;
    bool producer_1_success = true;

    // Consumer-local aggregation (TotalItems' collector):
    std::vector<int> consumer_0_results;
    std::vector<int> consumer_1_results;


    // Start consumers:
    std::thread consumer_0{[&q, &consumer_0_results] {
        while (true) {
            auto item = q.pop();
            if (item.has_value()) {
                consumer_0_results.push_back(item.value());
            } else
                break;
        }
    }};

    std::thread consumer_1{[&q, &consumer_1_results] {
        while (true) {
            auto item = q.pop();
            if (item.has_value()) {
                consumer_1_results.push_back(item.value());
            } else
                break;
        }
    }};


    // Start producers:
    std::thread producer_0{[&q, &producer_0_success] {
        for (int item = 0; item < kItemsPerProducer; ++item) {
            const bool pushed = q.push(item);
            if (!pushed) {
                producer_0_success =
                    false;  // record failure in THIS producer's local outcome
                return;
            }
        }
    }};

    std::thread producer_1{[&q, &producer_1_success] {
        for (int item = kItemsPerProducer; item < kTotalItems; ++item) {
            const bool pushed = q.push(item);
            if (!pushed) {
                producer_1_success = false;
                return;
            }
        }
    }};


    // =========================================================================
    // Phase boundary 1:
    // Production must be completely finished before close().
    // =========================================================================
    producer_0.join();
    producer_1.join();

    // Verify both producer-local outcomes before moving to close() phase
    if (!producer_0_success || !producer_1_success) {
        q.close();
        consumer_0.join();
        consumer_1.join();
        FAIL()
            << "producers unexpectedly failed to push before calling close()";
    }


    // =========================================================================
    // Phase transition:
    // no more production -> closed/draining
    // =========================================================================
    q.close();


    // =========================================================================
    // Phase boundary 2:
    // Consumers should drain all committed items, observe nullopt, and
    // terminate.
    // =========================================================================
    consumer_0.join();
    consumer_1.join();


    // =========================================================================
    // Main-thread verification:
    //
    // After join(), Main can safely merge & sort all consumer-local vectors.
    // =========================================================================
    std::vector<int> all_consumed;
    all_consumed.reserve(kTotalItems);

    // Merge consumer_0_results + consumer_1_results:
    all_consumed.insert(all_consumed.end(), consumer_0_results.begin(),
                        consumer_0_results.end());
    all_consumed.insert(all_consumed.end(), consumer_1_results.begin(),
                        consumer_1_results.end());

    // Verify raw consumed count == kTotalItems (avoid UB):
    ASSERT_EQ(all_consumed.size(), kTotalItems);

    // Sort all_consumed:
    std::sort(all_consumed.begin(), all_consumed.end());

    // Verify fainal results:
    for (int i = 0; i < kTotalItems; ++i) {
        EXPECT_EQ(all_consumed[i], i);
    }
}
