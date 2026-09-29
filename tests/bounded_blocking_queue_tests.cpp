#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

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

    // Trigger push:
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

    // Trigger pop:
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
