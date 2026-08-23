#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/coroutine/async_event.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Coroutine {

template <typename T> struct DefaultItemSize {
  uint64_t operator()(const T&) const { return 1; }
};

/**
 * SharedCapacity manages a shared capacity limit across one or more AsyncQueue instances,
 * acting as an asynchronous weighted semaphore built on AsyncEvent.
 *
 * Preserves strict FIFO arrival ordering for capacity acquisition.
 */
class SharedCapacity {
public:
  explicit SharedCapacity(std::optional<uint64_t> max_size = std::nullopt);
  ~SharedCapacity();

  SharedCapacity(const SharedCapacity&) = delete;
  SharedCapacity& operator=(const SharedCapacity&) = delete;
  SharedCapacity(SharedCapacity&&) = delete;
  SharedCapacity& operator=(SharedCapacity&&) = delete;

  std::optional<uint64_t> maxSize() const { return max_size_; }
  uint64_t currentSize() const { return current_size_; }

  bool tryAcquire(uint64_t size);
  Task<absl::Status> acquire(uint64_t size);
  void release(uint64_t size);

private:
  bool hasCapacity(uint64_t additional_size) const;
  bool canAcquire(uint64_t size) const;

  const std::optional<uint64_t> max_size_;
  uint64_t current_size_{0};
  std::list<uint64_t> waiters_;
  AsyncEvent capacity_event_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

using SharedCapacityPtr = std::shared_ptr<SharedCapacity>;

/**
 * AsyncQueue is a FIFO queue designed for Envoy coroutines.
 *
 * It models push() and pop() as coroutines, coordinating with SharedCapacity for producer
 * capacity backpressure and AsyncEvent for consumer emptiness notifications.
 */
template <typename T, typename SizeFunc = DefaultItemSize<T>> class AsyncQueue {
public:
  static_assert(
      std::is_invocable_r_v<uint64_t, SizeFunc, const T&>,
      "SizeFunc must be callable with 'const T&' and return a type convertible to uint64_t");

  explicit AsyncQueue(SharedCapacityPtr capacity = nullptr, SizeFunc size_func = SizeFunc())
      : capacity_(capacity != nullptr ? std::move(capacity)
                                      : std::make_shared<SharedCapacity>(std::nullopt)),
        size_func_(std::move(size_func)) {}

  explicit AsyncQueue(std::optional<uint64_t> max_size, SizeFunc size_func = SizeFunc())
      : AsyncQueue(max_size.has_value() ? std::make_shared<SharedCapacity>(*max_size) : nullptr,
                   std::move(size_func)) {}

  explicit AsyncQueue(uint64_t max_size, SizeFunc size_func = SizeFunc())
      : AsyncQueue(std::make_shared<SharedCapacity>(max_size), std::move(size_func)) {}

  ~AsyncQueue() {
    *alive_ = false;
    close();
    queue_.clear();
    if (current_size_ > 0) {
      capacity_->release(std::exchange(current_size_, 0));
    }
  }

  AsyncQueue(const AsyncQueue&) = delete;
  AsyncQueue& operator=(const AsyncQueue&) = delete;
  AsyncQueue(AsyncQueue&&) = delete;
  AsyncQueue& operator=(AsyncQueue&&) = delete;

  uint64_t itemCount() const { return queue_.size(); }
  uint64_t currentSize() const { return current_size_; }
  std::optional<uint64_t> maxSize() const { return capacity_->maxSize(); }
  bool isClosed() const { return closed_; }
  bool closed() const { return closed_; }
  bool empty() const { return queue_.empty(); }
  SharedCapacityPtr capacity() const { return capacity_; }

  /**
   * Non-blocking attempt to push an item. Returns true on success, false if capacity full or
   * closed.
   */
  bool tryPush(T item) {
    if (closed_) {
      return false;
    }
    if (queue_.empty() && items_event_.hasWaiters()) {
      queue_.push_back(QueuedItem{std::move(item), /*size=*/0});
      items_event_.notifyOne();
      return true;
    }
    uint64_t size = size_func_(item);
    if (!capacity_->tryAcquire(size)) {
      return false;
    }
    current_size_ += size;
    queue_.push_back(QueuedItem{std::move(item), size});
    items_event_.notifyOne();
    return true;
  }

  /**
   * Asynchronously pushes an item into the queue, suspending until capacity is available in
   * SharedCapacity. Returns OkStatus on admission, or FailedPrecondition if closed.
   */
  Task<absl::Status> push(T item) {
    if (closed_) {
      co_return absl::FailedPreconditionError("queue is closed");
    }

    if (queue_.empty() && items_event_.hasWaiters()) {
      queue_.push_back(QueuedItem{std::move(item), /*size=*/0});
      items_event_.notifyOne();
      co_return absl::OkStatus();
    }

    uint64_t size = size_func_(item);
    auto alive = alive_;
    auto cap = capacity_;

    auto status = co_await cap->acquire(size);
    if (!status.ok()) {
      co_return status; // Cancelled
    }

    if (!*alive || closed_) {
      cap->release(size);
      co_return absl::FailedPreconditionError("queue is closed");
    }

    current_size_ += size;
    queue_.push_back(QueuedItem{std::move(item), size});
    items_event_.notifyOne();
    co_return absl::OkStatus();
  }

  /**
   * Non-blocking attempt to pop an item. Returns the item if present, std::nullopt if empty.
   */
  std::optional<T> tryPop() {
    if (queue_.empty()) {
      return std::nullopt;
    }
    QueuedItem item = std::move(queue_.front());
    queue_.pop_front();
    if (item.size > 0) {
      current_size_ -= item.size;
      capacity_->release(item.size);
    }
    return std::move(item.item);
  }

  /**
   * Asynchronously pops an item from the queue, suspending until an item is available or the queue
   * is closed. Returns the item on success, or std::nullopt on EOF (closed and drained).
   */
  Task<absl::StatusOr<std::optional<T>>> pop() {
    auto alive = alive_;
    while (queue_.empty()) {
      if (closed_) {
        co_return std::optional<T>(std::nullopt); // EOF
      }
      auto status = co_await items_event_.wait();
      if (!status.ok()) {
        co_return status; // Cancelled
      }
      if (!*alive) {
        co_return std::optional<T>(std::nullopt);
      }
    }

    QueuedItem item = std::move(queue_.front());
    queue_.pop_front();
    if (item.size > 0) {
      current_size_ -= item.size;
      capacity_->release(item.size);
    }
    co_return std::optional<T>(std::move(item.item));
  }

  /**
   * Closes the queue for future pushes.
   * Queued items remain available for popping; once drained, pop() returns std::nullopt.
   */
  void close() {
    if (closed_) {
      return;
    }
    closed_ = true;
    items_event_.notifyAll();
  }

private:
  struct QueuedItem {
    T item;
    uint64_t size;
  };

  SharedCapacityPtr capacity_;
  SizeFunc size_func_;
  uint64_t current_size_{0};
  std::deque<QueuedItem> queue_;
  bool closed_{false};
  AsyncEvent items_event_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace Coroutine
} // namespace Envoy
