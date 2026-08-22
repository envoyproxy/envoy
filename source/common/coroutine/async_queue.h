#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Coroutine {

template <typename T> struct DefaultItemSize {
  uint64_t operator()(const T&) const { return 1; }
};

/**
 * SharedCapacity manages a shared capacity limit across one or more AsyncQueue instances.
 * Capacity is measured in abstract units (e.g. item count, bytes, or memory weight).
 */
class SharedCapacity {
public:
  using SpaceCallback = absl::AnyInvocable<void()>;

  explicit SharedCapacity(std::optional<uint64_t> max_size = std::nullopt);
  ~SharedCapacity();

  std::optional<uint64_t> maxSize() const { return max_size_; }
  uint64_t currentSize() const { return current_size_; }

  bool hasCapacity(uint64_t additional_size) const;
  void acquire(uint64_t size);
  void release(uint64_t size);

  uint64_t addSpaceAvailableCallback(SpaceCallback cb);
  void removeSpaceAvailableCallback(uint64_t id);

private:
  void notifySpaceAvailable();
  void cleanupRemoved();

  struct CallbackEntry {
    SpaceCallback cb;
    uint64_t id;
    bool removed{false};
  };

  const std::optional<uint64_t> max_size_;
  uint64_t current_size_{0};
  std::vector<CallbackEntry> callbacks_;
  uint64_t next_callback_id_{0};
  bool notifying_{false};
};

using SharedCapacityPtr = std::shared_ptr<SharedCapacity>;

/**
 * AsyncQueue is an asynchronous, coroutine-awaitable bounded queue.
 *
 * Capacity is measured in abstract units determined by SizeFunc.
 *   - By default, DefaultItemSize counts 1 unit per item (item count capacity).
 *   - Custom size functors can measure capacity in bytes, weight, or memory cost.
 *
 * When currentSize() + itemSize > maxSize(), push() suspends the pushing coroutine
 * until space is freed by pop().
 *
 * Multiple AsyncQueue instances can share a single SharedCapacity object to enforce
 * an aggregate capacity limit across multiple queues in a pipeline or fan-out topology.
 */
template <typename T, typename SizeFunc = DefaultItemSize<T>> class AsyncQueue {
  static_assert(
      std::is_invocable_r_v<uint64_t, SizeFunc, const T&>,
      "SizeFunc must be callable with 'const T&' and return a type convertible to uint64_t");

private:
  struct QueuedItem {
    T item;
    uint64_t size;
  };

  using PushWaiterCallback = absl::AnyInvocable<void(absl::Status)>;

  struct PushWaiter {
    T item;
    uint64_t size;
    PushWaiterCallback callback;
  };

  using PopWaiterCallback = absl::AnyInvocable<void(absl::StatusOr<std::optional<T>>)>;

  struct PopWaiter {
    PopWaiterCallback callback;
  };

  using PushWaiterList = std::list<PushWaiter>;
  using PopWaiterList = std::list<PopWaiter>;
  using PushWaiterIt = typename PushWaiterList::iterator;
  using PopWaiterIt = typename PopWaiterList::iterator;

public:
  class PushAwaitable : public LeafAwaitable<absl::Status> {
  public:
    PushAwaitable(AsyncQueue& queue, T item, uint64_t size)
        : queue_(queue), item_(std::move(item)), size_(size) {}

    PushAwaitable(PushAwaitable&&) noexcept = default;
    PushAwaitable& operator=(PushAwaitable&&) noexcept = delete;
    PushAwaitable(const PushAwaitable&) = delete;
    PushAwaitable& operator=(const PushAwaitable&) = delete;

  protected:
    std::optional<absl::Status> tryImmediate() override {
      if (queue_.closed_) {
        return absl::FailedPreconditionError("queue is closed");
      }
      if (queue_.tryPush(std::move(item_))) {
        return absl::OkStatus();
      }
      return std::nullopt;
    }

    void onStart() override {
      waiter_it_ = queue_.addPushWaiter(std::move(item_), size_,
                                        [this](absl::Status s) { this->complete(std::move(s)); });
    }

    void onCancel() override {
      if (waiter_it_.has_value()) {
        queue_.removePushWaiter(*waiter_it_);
        waiter_it_.reset();
      }
    }

  private:
    friend class AsyncQueue;

    AsyncQueue& queue_;
    T item_;
    uint64_t size_;
    std::optional<PushWaiterIt> waiter_it_;
  };

  class PopAwaitable : public LeafAwaitable<absl::StatusOr<std::optional<T>>> {
  public:
    explicit PopAwaitable(AsyncQueue& queue) : queue_(queue) {}

    PopAwaitable(PopAwaitable&&) noexcept = default;
    PopAwaitable& operator=(PopAwaitable&&) noexcept = delete;
    PopAwaitable(const PopAwaitable&) = delete;
    PopAwaitable& operator=(const PopAwaitable&) = delete;

  protected:
    std::optional<absl::StatusOr<std::optional<T>>> tryImmediate() override {
      if (std::optional<T> item = queue_.tryPop()) {
        return std::make_optional(std::move(item));
      }
      if (queue_.closed_) {
        return std::optional<T>(std::nullopt);
      }
      return std::nullopt;
    }

    void onStart() override {
      waiter_it_ = queue_.addPopWaiter(
          [this](absl::StatusOr<std::optional<T>> res) { this->complete(std::move(res)); });
    }

    void onCancel() override {
      if (waiter_it_.has_value()) {
        queue_.removePopWaiter(*waiter_it_);
        waiter_it_.reset();
      }
    }

  private:
    friend class AsyncQueue;

    AsyncQueue& queue_;
    std::optional<PopWaiterIt> waiter_it_;
  };

  explicit AsyncQueue(std::optional<uint64_t> max_size = std::nullopt,
                      SizeFunc size_func = SizeFunc{})
      : AsyncQueue(std::make_shared<SharedCapacity>(max_size), std::move(size_func)) {}

  explicit AsyncQueue(SharedCapacityPtr capacity, SizeFunc size_func = SizeFunc{})
      : capacity_(capacity != nullptr ? std::move(capacity)
                                      : std::make_shared<SharedCapacity>(std::nullopt)),
        size_func_(std::move(size_func)) {
    callback_id_ = capacity_->addSpaceAvailableCallback([this]() { maybeUnblockPushers(); });
  }

  ~AsyncQueue() {
    if (callback_id_ != 0) {
      capacity_->removeSpaceAvailableCallback(callback_id_);
      callback_id_ = 0;
    }
    if (!closed_) {
      close();
    }
    if (current_size_ > 0) {
      capacity_->release(current_size_);
      current_size_ = 0;
    }
  }

  // AsyncQueue is non-copyable and non-movable.
  AsyncQueue(const AsyncQueue&) = delete;
  AsyncQueue& operator=(const AsyncQueue&) = delete;
  AsyncQueue(AsyncQueue&&) = delete;
  AsyncQueue& operator=(AsyncQueue&&) = delete;

  // Current occupancy / size of items in this queue in abstract units.
  uint64_t currentSize() const { return current_size_; }

  // Number of items in queue.
  size_t itemCount() const { return queue_.size(); }

  // Capacity limit (std::nullopt means unlimited).
  std::optional<uint64_t> maxSize() const { return capacity_->maxSize(); }

  // Access to the shared capacity object.
  const SharedCapacityPtr& capacity() const { return capacity_; }

  bool empty() const { return queue_.empty(); }
  bool closed() const { return closed_; }

  // Pushes an item asynchronously into the queue.
  // Returns a LeafAwaitable which completes immediately if space is available or fast-fails
  // without suspending.
  PushAwaitable push(T item) {
    uint64_t item_size = size_func_(item);
    return PushAwaitable(*this, std::move(item), item_size);
  }

  // Non-blocking synchronous push attempt.
  // Returns true if pushed, false if capacity exceeded or closed.
  template <typename U = T> bool tryPush(U&& item) {
    if (closed_) {
      return false;
    }

    if (!pop_waiters_.empty()) {
      PopWaiterCallback waiter = std::move(pop_waiters_.front().callback);
      pop_waiters_.pop_front();
      waiter(std::make_optional(std::forward<U>(item)));
      return true;
    }

    if (!push_waiters_.empty()) {
      return false;
    }

    uint64_t item_size = size_func_(item);
    if (!capacity_->hasCapacity(item_size) && (capacity_->currentSize() != 0 || !queue_.empty())) {
      return false;
    }

    capacity_->acquire(item_size);
    current_size_ += item_size;
    queue_.push_back(QueuedItem{std::forward<U>(item), item_size});
    return true;
  }

  // Pops the next item asynchronously.
  // Returns a LeafAwaitable which completes immediately if items are available or fast-fails
  // without suspending.
  PopAwaitable pop() { return PopAwaitable(*this); }

  // Non-blocking synchronous pop attempt.
  // Returns the front item if present, std::nullopt if queue is empty.
  std::optional<T> tryPop() {
    if (!queue_.empty()) {
      QueuedItem queued = std::move(queue_.front());
      queue_.pop_front();
      current_size_ -= queued.size;
      capacity_->release(queued.size);
      return std::make_optional(std::move(queued.item));
    }

    if (!push_waiters_.empty()) {
      PushWaiter waiter = std::move(push_waiters_.front());
      push_waiters_.pop_front();
      waiter.callback(absl::OkStatus());
      return std::make_optional(std::move(waiter.item));
    }

    return std::nullopt;
  }

  // Closes the queue for future pushes.
  // Queued items remain available for popping; once drained, pop() returns std::nullopt.
  void close() {
    if (closed_) {
      return;
    }
    closed_ = true;

    if (queue_.empty()) {
      while (!pop_waiters_.empty()) {
        PopWaiterCallback waiter = std::move(pop_waiters_.front().callback);
        pop_waiters_.pop_front();
        waiter(std::nullopt);
      }
    }

    while (!push_waiters_.empty()) {
      PushWaiterCallback waiter = std::move(push_waiters_.front().callback);
      push_waiters_.pop_front();
      waiter(absl::FailedPreconditionError("queue closed"));
    }
  }

private:
  PushWaiterIt addPushWaiter(T item, uint64_t size,
                             absl::AnyInvocable<void(absl::Status)> callback) {
    return push_waiters_.insert(push_waiters_.end(),
                                PushWaiter{std::move(item), size, std::move(callback)});
  }

  void removePushWaiter(PushWaiterIt it) { push_waiters_.erase(it); }

  PopWaiterIt addPopWaiter(PopWaiterCallback callback) {
    return pop_waiters_.insert(pop_waiters_.end(), PopWaiter{std::move(callback)});
  }

  void removePopWaiter(PopWaiterIt it) { pop_waiters_.erase(it); }

  void maybeUnblockPushers() {
    while (!push_waiters_.empty()) {
      const PushWaiter& front = push_waiters_.front();
      if (capacity_->hasCapacity(front.size) || (capacity_->currentSize() == 0 && queue_.empty())) {
        PushWaiter waiter = std::move(push_waiters_.front());
        push_waiters_.pop_front();

        capacity_->acquire(waiter.size);
        current_size_ += waiter.size;
        queue_.push_back(QueuedItem{std::move(waiter.item), waiter.size});
        waiter.callback(absl::OkStatus());
      } else {
        break;
      }
    }
  }

  SharedCapacityPtr capacity_;
  SizeFunc size_func_;
  uint64_t current_size_{0};
  std::deque<QueuedItem> queue_;
  PushWaiterList push_waiters_;
  PopWaiterList pop_waiters_;
  bool closed_{false};
  uint64_t callback_id_{0};
};

} // namespace Coroutine
} // namespace Envoy
