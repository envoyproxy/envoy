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
 *
 * Requests for capacity are queued in a global FIFO wait queue when capacity is exhausted,
 * preventing starvation of large allocations and ensuring fair temporal ordering.
 */
class SharedCapacity {
public:
  using GrantCallback = absl::AnyInvocable<void()>;

private:
  struct Waiter {
    uint64_t size;
    GrantCallback cb;
  };

public:
  using WaiterIterator = std::list<Waiter>::iterator;

  explicit SharedCapacity(std::optional<uint64_t> max_size = std::nullopt);
  ~SharedCapacity();

  SharedCapacity(const SharedCapacity&) = delete;
  SharedCapacity& operator=(const SharedCapacity&) = delete;
  SharedCapacity(SharedCapacity&&) = delete;
  SharedCapacity& operator=(SharedCapacity&&) = delete;

  std::optional<uint64_t> maxSize() const { return max_size_; }
  uint64_t currentSize() const { return current_size_; }

  bool hasCapacity(uint64_t additional_size) const;
  void release(uint64_t size);

  bool tryAcquire(uint64_t size);
  WaiterIterator requestCapacity(uint64_t size, GrantCallback cb);
  void cancelRequest(WaiterIterator it);

private:
  bool canAcquire(uint64_t size) const;
  void acquire(uint64_t size);
  void processWaiters();

  const std::optional<uint64_t> max_size_;
  uint64_t current_size_{0};
  std::list<Waiter> waiters_;
  bool processing_waiters_{false};
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
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
    std::optional<SharedCapacity::WaiterIterator> capacity_waiter_it;
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
        size_func_(std::move(size_func)) {}

  ~AsyncQueue() {
    for (auto it = push_waiters_.rbegin(); it != push_waiters_.rend(); ++it) {
      if (it->capacity_waiter_it.has_value()) {
        capacity_->cancelRequest(*it->capacity_waiter_it);
        it->capacity_waiter_it.reset();
      }
    }

    if (current_size_ > 0) {
      uint64_t size = current_size_;
      current_size_ = 0;
      queue_.clear();
      capacity_->release(size);
    }
    if (!closed_) {
      close();
    }
    *alive_ = false;
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
    if (!capacity_->tryAcquire(item_size)) {
      return false;
    }

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
      if (waiter.capacity_waiter_it.has_value()) {
        capacity_->cancelRequest(*waiter.capacity_waiter_it);
        waiter.capacity_waiter_it.reset();
      }
      T item = std::move(waiter.item);
      PushWaiterCallback cb = std::move(waiter.callback);
      cb(absl::OkStatus());
      return std::make_optional(std::move(item));
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

    auto alive = alive_;

    while (!pop_waiters_.empty()) {
      PopWaiterCallback waiter = std::move(pop_waiters_.front().callback);
      pop_waiters_.pop_front();
      waiter(std::nullopt);
      if (!*alive) {
        return;
      }
    }

    for (auto it = push_waiters_.rbegin(); it != push_waiters_.rend(); ++it) {
      if (it->capacity_waiter_it.has_value()) {
        auto cap_it = *it->capacity_waiter_it;
        it->capacity_waiter_it.reset();
        capacity_->cancelRequest(cap_it);
        if (!*alive) {
          return;
        }
      }
    }

    while (!push_waiters_.empty()) {
      PushWaiter waiter = std::move(push_waiters_.front());
      push_waiters_.pop_front();
      PushWaiterCallback cb = std::move(waiter.callback);
      cb(absl::FailedPreconditionError("queue closed"));
      if (!*alive) {
        return;
      }
    }
  }

private:
  PushWaiterIt addPushWaiter(T item, uint64_t size, PushWaiterCallback callback) {
    auto it = push_waiters_.insert(
        push_waiters_.end(), PushWaiter{std::move(item), size, std::move(callback), std::nullopt});
    it->capacity_waiter_it =
        capacity_->requestCapacity(size, [this, it]() { onCapacityGranted(it); });
    return it;
  }

  void onCapacityGranted(PushWaiterIt it) {
    auto alive = alive_;
    uint64_t size = it->size;
    current_size_ += size;
    queue_.push_back(QueuedItem{std::move(it->item), size});

    PushWaiterCallback cb = std::move(it->callback);
    push_waiters_.erase(it);

    cb(absl::OkStatus());
  }

  void removePushWaiter(PushWaiterIt it) {
    auto cap_it = it->capacity_waiter_it;
    push_waiters_.erase(it);
    if (cap_it.has_value()) {
      capacity_->cancelRequest(*cap_it);
    }
  }

  PopWaiterIt addPopWaiter(PopWaiterCallback callback) {
    return pop_waiters_.insert(pop_waiters_.end(), PopWaiter{std::move(callback)});
  }

  void removePopWaiter(PopWaiterIt it) { pop_waiters_.erase(it); }

  SharedCapacityPtr capacity_;
  SizeFunc size_func_;
  uint64_t current_size_{0};
  std::deque<QueuedItem> queue_;
  PushWaiterList push_waiters_;
  PopWaiterList pop_waiters_;
  bool closed_{false};
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace Coroutine
} // namespace Envoy
