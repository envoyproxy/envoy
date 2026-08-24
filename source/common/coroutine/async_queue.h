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
#include "source/common/coroutine/any_of.h"
#include "source/common/coroutine/async_event.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Coroutine {

class SharedCapacity;

/**
 * CapacityReservation is an RAII guard holding a reservation against SharedCapacity.
 * When destroyed or explicitly released, it automatically decrements the reserved capacity.
 */
class CapacityReservation {
public:
  CapacityReservation() = default;
  CapacityReservation(std::shared_ptr<SharedCapacity> cap, uint64_t size);
  ~CapacityReservation();

  CapacityReservation(const CapacityReservation&) = delete;
  CapacityReservation& operator=(const CapacityReservation&) = delete;

  CapacityReservation(CapacityReservation&& other) noexcept;
  CapacityReservation& operator=(CapacityReservation&& other) noexcept;

  uint64_t size() const { return size_; }
  bool hasCapacity() const { return cap_ != nullptr && size_ > 0; }
  void release();

private:
  std::shared_ptr<SharedCapacity> cap_;
  uint64_t size_{0};
};

template <typename T> struct DefaultItemSize {
  uint64_t operator()(const T&) const { return 1; }
};

/**
 * SharedCapacity manages a shared capacity limit across one or more AsyncQueue instances,
 * acting as an asynchronous weighted semaphore built on AsyncEvent.
 *
 * Preserves strict FIFO arrival ordering for capacity acquisition.
 */
class SharedCapacity : public std::enable_shared_from_this<SharedCapacity> {
public:
  explicit SharedCapacity(std::optional<uint64_t> max_size = std::nullopt);
  ~SharedCapacity();

  SharedCapacity(const SharedCapacity&) = delete;
  SharedCapacity& operator=(const SharedCapacity&) = delete;
  SharedCapacity(SharedCapacity&&) = delete;
  SharedCapacity& operator=(SharedCapacity&&) = delete;

  std::optional<uint64_t> maxSize() const { return max_size_; }
  uint64_t currentSize() const { return current_size_; }

  std::optional<CapacityReservation> tryAcquire(uint64_t size);
  Task<absl::StatusOr<CapacityReservation>> acquire(uint64_t size);
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

inline CapacityReservation::CapacityReservation(std::shared_ptr<SharedCapacity> cap, uint64_t size)
    : cap_(std::move(cap)), size_(size) {}

inline CapacityReservation::~CapacityReservation() { release(); }

inline CapacityReservation::CapacityReservation(CapacityReservation&& other) noexcept
    : cap_(std::move(other.cap_)), size_(std::exchange(other.size_, 0)) {}

inline CapacityReservation& CapacityReservation::operator=(CapacityReservation&& other) noexcept {
  if (this != &other) {
    release();
    cap_ = std::move(other.cap_);
    size_ = std::exchange(other.size_, 0);
  }
  return *this;
}

inline void CapacityReservation::release() {
  if (cap_ != nullptr && size_ > 0) {
    auto cap = std::move(cap_);
    const uint64_t size = std::exchange(size_, 0);
    cap->release(size);
  } else {
    cap_.reset();
    size_ = 0;
  }
}

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
    current_size_ = 0;
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
    if (items_event_.hasWaiters()) {
      queue_.push_back(QueuedItem{std::move(item), CapacityReservation{}});
      items_event_.notifyOne();
      return true;
    }
    uint64_t size = size_func_(item);
    auto res_opt = capacity_->tryAcquire(size);
    if (!res_opt.has_value()) {
      return false;
    }
    current_size_ += size;
    queue_.push_back(QueuedItem{std::move(item), std::move(*res_opt)});
    items_event_.notifyOne();
    return true;
  }

  /**
   * Asynchronously pushes an item into the queue, suspending until capacity is available in
   * SharedCapacity or directly handing off to a popper via any_of.
   * Returns OkStatus on admission/handoff, or FailedPrecondition if closed.
   */
  Task<absl::Status> push(T item) {
    if (closed_) {
      co_return absl::FailedPreconditionError("queue is closed");
    }

    if (items_event_.hasWaiters()) {
      queue_.push_back(QueuedItem{std::move(item), CapacityReservation{}});
      items_event_.notifyOne();
      co_return absl::OkStatus();
    }

    uint64_t size = size_func_(item);
    auto res_opt = capacity_->tryAcquire(size);
    if (res_opt.has_value()) {
      current_size_ += size;
      queue_.push_back(QueuedItem{std::move(item), std::move(*res_opt)});
      items_event_.notifyOne();
      co_return absl::OkStatus();
    }

    auto alive = alive_;
    auto cap = capacity_;

    auto res = co_await any_of(cap->acquire(size), waitForPopper(&item));
    CO_RETURN_IF_ERROR(res.status());

    if (res.value().index() == 0) {
      auto reservation = std::move(absl::get<0>(res.value()));
      if (!*alive || closed_) {
        co_return absl::FailedPreconditionError("queue is closed");
      }

      current_size_ += size;
      queue_.push_back(QueuedItem{std::move(item), std::move(reservation)});
      items_event_.notifyOne();
      co_return absl::OkStatus();
    }

    co_return absl::OkStatus();
  }

  /**
   * Non-blocking attempt to pop an item. Returns the item if present or available from a waiting
   * pusher, std::nullopt otherwise.
   */
  std::optional<T> tryPop() {
    if (!queue_.empty()) {
      QueuedItem item = std::move(queue_.front());
      queue_.pop_front();
      current_size_ -= item.reservation.size();
      return std::move(item.item);
    }

    while (!pusher_waiters_.empty()) {
      auto waiter = std::move(pusher_waiters_.front());
      pusher_waiters_.pop_front();
      if (*waiter.active && waiter.cb) {
        *waiter.active = false;
        T item = std::move(*waiter.item_ptr);
        waiter.cb(absl::OkStatus());
        return std::move(item);
      }
    }

    return std::nullopt;
  }

  /**
   * Asynchronously pops an item from the queue, suspending until an item is available, directly
   * handed off from a waiting pusher, or the queue is closed. Returns the item on success, or
   * std::nullopt on EOF (closed and drained).
   */
  Task<absl::StatusOr<std::optional<T>>> pop() {
    auto alive = alive_;
    while (true) {
      if (!queue_.empty()) {
        QueuedItem item = std::move(queue_.front());
        queue_.pop_front();
        current_size_ -= item.reservation.size();
        co_return std::optional<T>(std::move(item.item));
      }

      while (!pusher_waiters_.empty()) {
        auto waiter = std::move(pusher_waiters_.front());
        pusher_waiters_.pop_front();
        if (*waiter.active && waiter.cb) {
          *waiter.active = false;
          T item = std::move(*waiter.item_ptr);
          waiter.cb(absl::OkStatus());
          co_return std::optional<T>(std::move(item));
        }
      }

      if (closed_) {
        co_return std::optional<T>(std::nullopt); // EOF
      }

      CO_RETURN_IF_ERROR(co_await items_event_.wait());
      if (!*alive) {
        co_return std::optional<T>(std::nullopt);
      }
    }
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

    std::list<PusherWaiter> pusher_batch;
    pusher_batch.swap(pusher_waiters_);
    while (!pusher_batch.empty()) {
      auto waiter = std::move(pusher_batch.front());
      pusher_batch.pop_front();
      if (*waiter.active && waiter.cb) {
        *waiter.active = false;
        waiter.cb(absl::FailedPreconditionError("queue is closed"));
      }
    }
  }

private:
  struct QueuedItem {
    T item;
    CapacityReservation reservation;
  };

  struct PusherWaiter {
    absl::AnyInvocable<void(absl::Status)> cb;
    T* item_ptr;
    std::shared_ptr<bool> active;
  };

  class PopperRendezvousAwaitable : public LeafAwaitable<absl::Status> {
  public:
    PopperRendezvousAwaitable(AsyncQueue& queue, T* item_ptr)
        : queue_(queue), item_ptr_(item_ptr) {}

  protected:
    void onStart() override {
      auto cb = [this](absl::Status status) { this->complete(status); };
      queue_.pusher_waiters_.push_back(PusherWaiter{std::move(cb), item_ptr_, active_});
    }

    void onCancel() override { *active_ = false; }

  private:
    AsyncQueue& queue_;
    T* item_ptr_;
    std::shared_ptr<bool> active_{std::make_shared<bool>(true)};
  };

  PopperRendezvousAwaitable waitForPopper(T* item_ptr) {
    return PopperRendezvousAwaitable(*this, item_ptr);
  }

  SharedCapacityPtr capacity_;
  SizeFunc size_func_;
  uint64_t current_size_{0};
  std::deque<QueuedItem> queue_;
  std::list<PusherWaiter> pusher_waiters_;
  bool closed_{false};
  AsyncEvent items_event_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace Coroutine
} // namespace Envoy
