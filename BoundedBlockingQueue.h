#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

template <typename T> class BoundedBlockingQueue {
public:
    explicit BoundedBlockingQueue(std::size_t capacity) : capacity_(capacity) {}

    // false => queue has been closed
    bool push(T item);

    // nullopt => queue is "closed && empty"
    std::optional<T> pop();

    void close();

private:
    std::queue<T> buffer_;
    const std::size_t capacity_;

    bool closed_{false};

    std::mutex mutex_;

    // Consumers wait here for:
    // !buffer_.empty() || closed_
    std::condition_variable not_empty_cv_;

    // Producers wait here for:
    // buffer_.size() < capacity_ || closed_
    std::condition_variable not_full_cv_;
};

template <typename T> inline bool BoundedBlockingQueue<T>::push(T item) {
    std::unique_lock<std::mutex> lock(mutex_);  // need flexible

    not_full_cv_.wait(lock, [this] {
        return buffer_.size() < capacity_ || closed_;
    });  // producer checks predicate
    if (closed_) {
        return false;  // queue terminated already
    }

    buffer_.push(std::move(item));  // push
    lock.unlock();                  // release mutex early
    not_empty_cv_.notify_one();     // notify consumer

    return true;
}

template <typename T> inline std::optional<T> BoundedBlockingQueue<T>::pop() {
    std::unique_lock<std::mutex> lock(mutex_);  // need flexible

    not_empty_cv_.wait(lock, [this] {
        return !buffer_.empty() || closed_;
    });  // consumer checks predicate
    if (buffer_.empty()) {
        return std::nullopt;  // queue is "closed && empty
                              // (no more items to pop)
    }

    std::optional<T> item = std::move(buffer_.front());  // move
    buffer_.pop();                                       // pop
    lock.unlock();                                       // release mutex early
    not_full_cv_.notify_one();                           // notify producer

    return item;
}

template <typename T> inline void BoundedBlockingQueue<T>::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);  // simple fixed scope
        if (closed_) {                             // idempotent
            return;
        }
        closed_ = true;
    }

    // wake up all threads
    not_full_cv_.notify_all();
    not_empty_cv_.notify_all();
}
