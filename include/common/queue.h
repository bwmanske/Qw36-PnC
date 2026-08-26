#ifndef PC_QUEUE_H
#define PC_QUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace pc {

template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity);

    void push(T item);
    T pop();

    bool try_push(T item, std::chrono::milliseconds timeout);
    std::optional<T> try_pop(std::chrono::milliseconds timeout);

    size_t size() const;
    bool empty() const;
    void shutdown();

private:
    std::deque<T> buffer_;
    size_t capacity_;
    mutable std::mutex mtx_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    bool shutdown_ = false;
};

// Template implementations must be in header

template<typename T>
BoundedQueue<T>::BoundedQueue(size_t capacity)
    : capacity_(capacity) {}

template<typename T>
void BoundedQueue<T>::push(T item) {
    std::unique_lock<std::mutex> lock(mtx_);
    not_full_.wait(lock, [this]() {
        return buffer_.size() < capacity_ || shutdown_;
    });
    if (shutdown_) return;
    buffer_.push_back(std::move(item));
    not_empty_.notify_one();
}

template<typename T>
T BoundedQueue<T>::pop() {
    std::unique_lock<std::mutex> lock(mtx_);
    not_empty_.wait(lock, [this]() {
        return !buffer_.empty() || shutdown_;
    });
    if (shutdown_ && buffer_.empty()) {
        throw std::runtime_error("Queue shut down");
    }
    T item = std::move(buffer_.front());
    buffer_.pop_front();
    not_full_.notify_one();
    return item;
}

template<typename T>
bool BoundedQueue<T>::try_push(T item, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (!not_full_.wait_for(lock, timeout, [this]() {
        return buffer_.size() < capacity_ || shutdown_;
    })) return false;
    if (shutdown_) return false;
    buffer_.push_back(std::move(item));
    not_empty_.notify_one();
    return true;
}

template<typename T>
std::optional<T> BoundedQueue<T>::try_pop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (!not_empty_.wait_for(lock, timeout, [this]() {
        return !buffer_.empty() || shutdown_;
    })) return std::nullopt;
    if (buffer_.empty()) return std::nullopt;
    T item = std::move(buffer_.front());
    buffer_.pop_front();
    not_full_.notify_one();
    return item;
}

template<typename T>
size_t BoundedQueue<T>::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return buffer_.size();
}

template<typename T>
bool BoundedQueue<T>::empty() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return buffer_.empty();
}

template<typename T>
void BoundedQueue<T>::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    shutdown_ = true;
    not_full_.notify_all();
    not_empty_.notify_all();
}

} // namespace pc

#endif // PC_QUEUE_H
