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
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/semaphore.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Coroutine {

using Capacity = Semaphore;
using CapacityReservation = SemaphoreReservation;
using CapacityPtr = SemaphorePtr;

template <typename T> struct DefaultItemSize {
  uint64_t operator()(const T&) const { return 1; }
};

/**
 * AsyncQueue is an asynchronous, bounded or unbounded FIFO queue for Envoy coroutines.
 *
 * Concurrency & Ownership Model:
 * - Designed for multi-producer, single-consumer or single-producer, single-consumer patterns,
 *   where "producer" and "consumer" refer to independent coroutines pinned to the same
 *   executor/dispatcher thread.
 * - The queue is move-only and owned exclusively by the single consumer coroutine (popper).
 * - Producer coroutines access the queue via lightweight `PushAccessor` instances (holding a
 *   weak pointer), guaranteeing deterministic queue teardown when the consumer coroutine finishes.
 *
 * Direct Resumption & Push-Pop Rendezvous:
 * - Direct Resumption: If a consumer coroutine is awaiting pop(), push() hands off the data
 *   synchronously inline without buffering or consuming Semaphore capacity.
 * - Rendezvous: If a producer coroutine is suspended awaiting Semaphore capacity, an incoming
 *   pop() steals the item directly from queue_ without waiting for capacity. When capacity is
 *   eventually granted to the producer, RAII immediately returns the permits to the Semaphore.
 *
 * Reentrancy & Memory Safety:
 * - Synchronous reentrancy occurs at two well-defined points: direct data handoff in
 *   `tryHandoff()` and EOF notification in `close()`.
 * - In the single consumer coroutine model, call stack depth is strictly bounded at O(1).
 * - In-line queue destruction by a resumed consumer coroutine is safe: pop_waiters_ is updated
 *   prior to resumption, and no `this` members are touched after the callback returns.
 * - Any in-flight producer coroutines suspended on Semaphore acquisition observe `*alive_ == false`
 *   or `closed_ == true` upon waking up and terminate cleanly with FailedPreconditionError.
 */
template <typename T, typename SizeFunc = DefaultItemSize<T>> class AsyncQueue {
public:
  static_assert(
      std::is_invocable_r_v<uint64_t, SizeFunc, const T&>,
      "SizeFunc must be callable with 'const T&' and return a type convertible to uint64_t");

private:
  struct Core : public std::enable_shared_from_this<Core> {
    struct QueuedItem {
      explicit QueuedItem(T item_val) : item(std::move(item_val)) {}

      std::optional<T> item;
      std::optional<CapacityReservation> reservation;
    };

    struct PopWaiter {
      absl::AnyInvocable<void(absl::StatusOr<std::optional<T>>)> cb;
    };

    Core(CapacityPtr capacity, SizeFunc size_func)
        : capacity_(capacity != nullptr ? std::move(capacity)
                                        : std::make_shared<Capacity>(std::nullopt)),
          size_func_(std::move(size_func)) {}

    ~Core() {
      ASSERT(pop_waiters_.empty(),
             "under single consumer assumption, popper cannot be waiting upon destruction");
      if (in_handoff_ > 0) {
        ASSERT(queue_.empty() && pending_pushers_ == 0,
               "no queued items or pending pushers can exist when queue is destroyed during "
               "direct handoff");
      }
      *alive_ = false;
      close();
      queue_.clear();
      current_size_ = 0;
    }

    void close() {
      if (closed_) {
        return;
      }
      closed_ = true;
      std::list<std::shared_ptr<PopWaiter>> waiters;
      waiters.swap(pop_waiters_);
      for (auto& w : waiters) {
        if (w->cb) {
          auto cb = std::move(w->cb);
          cb(std::optional<T>(std::nullopt));
        }
      }
    }

    bool closed() const { return closed_; }

    bool empty() const { return queue_.empty(); }

    uint64_t itemCount() const { return queue_.size(); }

    uint64_t currentSize() const { return current_size_; }
    std::optional<uint64_t> maxSize() const { return capacity_->maxPermits(); }
    CapacityPtr capacity() const { return capacity_; }

    template <typename U = T> bool tryHandoff(U&& item) {
      while (!pop_waiters_.empty()) {
        if (!pop_waiters_.front()->cb) {
          pop_waiters_.pop_front();
          continue;
        }
        auto waiter = std::move(pop_waiters_.front());
        pop_waiters_.pop_front();

        auto cb = std::move(waiter->cb);
        if (cb) {
          auto alive = alive_;
          ++in_handoff_;
          cb(std::optional<T>(std::move(item)));
          if (*alive) {
            --in_handoff_;
          }
        }
        return true;
      }
      return false;
    }

    Task<absl::Status> push(T item) {
      if (closed_) {
        co_return absl::FailedPreconditionError("queue is closed");
      }

      // 1. Direct handoff to a waiting popper if available.
      if (tryHandoff(item)) {
        co_return absl::OkStatus();
      }

      // 2. Put item in queue_ as a pending entry and account for its size.
      const uint64_t size = size_func_(item);
      current_size_ += size;
      auto queued_item = std::make_shared<QueuedItem>(std::move(item));
      auto it = queue_.insert(queue_.end(), queued_item);

      // 3. Acquire capacity from Capacity. Any pop() arriving while suspended will steal from
      // queue_.
      auto alive = alive_;
      ++pending_pushers_;
      auto cap_res = co_await capacity_->acquire(size);
      if (!*alive) {
        queued_item->item.reset();
        co_return absl::FailedPreconditionError("queue is closed");
      }

      --pending_pushers_;

      // If a pop() stole the item during rendezvous, we are done!
      if (!queued_item->item.has_value()) {
        // cap_res is destroyed by RAII, returning permits back to capacity_.
        co_return absl::OkStatus();
      }

      if (!cap_res.ok() || closed_) {
        queue_.erase(it);
        if (queued_item->item.has_value()) {
          current_size_ -= size;
          queued_item->item.reset();
        }
        if (!cap_res.ok()) {
          co_return cap_res.status();
        }
        co_return absl::FailedPreconditionError("queue is closed");
      }

      // Attach the acquired reservation.
      queued_item->reservation = std::move(cap_res.value());
      co_return absl::OkStatus();
    }

    template <typename U = T> bool tryPush(U&& item) {
      if (closed_) {
        return false;
      }

      if (tryHandoff(std::forward<U>(item))) {
        return true;
      }

      const uint64_t size = size_func_(item);
      auto res_opt = capacity_->tryAcquire(size);
      if (!res_opt.has_value()) {
        return false;
      }

      current_size_ += size;
      auto queued_item = std::make_shared<QueuedItem>(std::forward<U>(item));
      queued_item->reservation = std::move(*res_opt);
      queue_.push_back(std::move(queued_item));
      return true;
    }

    std::optional<T> tryPop() {
      if (queue_.empty()) {
        return std::nullopt;
      }
      auto queued_item = std::move(queue_.front());
      queue_.pop_front();

      auto item = std::move(*queued_item->item);
      queued_item->item.reset();
      current_size_ -= size_func_(item);
      queued_item->reservation.reset();

      return std::optional<T>(std::move(item));
    }

    CapacityPtr capacity_;
    SizeFunc size_func_;
    uint64_t current_size_{0};
    uint64_t pending_pushers_{0};
    uint64_t in_handoff_{0};
    std::list<std::shared_ptr<QueuedItem>> queue_;
    std::list<std::shared_ptr<PopWaiter>> pop_waiters_;
    bool closed_{false};
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
  };

public:
  class PopAwaitable : public LeafAwaitable<absl::StatusOr<std::optional<T>>> {
  public:
    explicit PopAwaitable(std::shared_ptr<Core> core) : core_(std::move(core)) {}

  protected:
    std::optional<absl::StatusOr<std::optional<T>>> tryImmediate() override {
      std::optional<T> item = core_->tryPop();
      if (item.has_value()) {
        return item;
      }
      if (core_->closed()) {
        // Return immediate EOF without suspending. Note that returning
        // std::optional<T>(std::nullopt) wraps an empty inner optional (EOF) inside a present outer
        // optional, whereas returning std::nullopt would produce an empty outer optional that
        // instructs LeafAwaitable to suspend.
        return std::optional<T>(std::nullopt);
      }
      // Queue is open but empty: return an empty outer optional to suspend and wait for a producer.
      return std::nullopt;
    }

    void onStart() override {
      waiter_ = std::make_shared<typename Core::PopWaiter>();
      waiter_->cb = [this](absl::StatusOr<std::optional<T>> res) {
        this->complete(std::move(res));
      };
      core_->pop_waiters_.push_back(waiter_);
    }

    void onCancel() override {
      ASSERT(waiter_ != nullptr);
      waiter_->cb = nullptr;
    }

  private:
    std::shared_ptr<Core> core_;
    std::shared_ptr<typename Core::PopWaiter> waiter_;
  };

  /**
   * PushAccessor provides non-owning access to an AsyncQueue.
   * Producers can push to the queue without holding ownership, allowing the consumer (the popper)
   * to be the sole owner of the queue lifetime.
   */
  class PushAccessor {
  public:
    PushAccessor() = default;
    explicit PushAccessor(std::weak_ptr<Core> core) : core_(std::move(core)) {}

    Task<absl::Status> push(T item) {
      auto core = core_.lock();
      if (!core || core->closed()) {
        co_return absl::FailedPreconditionError("queue is closed");
      }
      auto push_task = core->push(std::move(item));
      core.reset();
      co_return co_await std::move(push_task);
    }

    template <typename U = T> bool tryPush(U&& item) {
      auto core = core_.lock();
      if (!core || core->closed()) {
        return false;
      }
      Core* core_ptr = core.get();
      core.reset();
      return core_ptr->tryPush(std::forward<U>(item));
    }

    void close() {
      auto core = core_.lock();
      if (core) {
        core->close();
      }
    }

    bool closed() const {
      auto core = core_.lock();
      return !core || core->closed();
    }

    bool empty() const {
      auto core = core_.lock();
      return !core || core->empty();
    }

    uint64_t currentSize() const {
      auto core = core_.lock();
      return core ? core->currentSize() : 0;
    }

    uint64_t itemCount() const {
      auto core = core_.lock();
      return core ? core->itemCount() : 0;
    }

    CapacityPtr capacity() const {
      auto core = core_.lock();
      return core ? core->capacity() : nullptr;
    }

  private:
    std::weak_ptr<Core> core_;
  };

  explicit AsyncQueue(CapacityPtr capacity = nullptr, SizeFunc size_func = SizeFunc())
      : core_(std::make_shared<Core>(std::move(capacity), std::move(size_func))) {}

  explicit AsyncQueue(std::optional<uint64_t> max_size, SizeFunc size_func = SizeFunc())
      : AsyncQueue(max_size.has_value() ? std::make_shared<Capacity>(*max_size) : nullptr,
                   std::move(size_func)) {}

  explicit AsyncQueue(uint64_t max_size, SizeFunc size_func = SizeFunc())
      : AsyncQueue(std::make_shared<Capacity>(max_size), std::move(size_func)) {}

  ~AsyncQueue() {
    if (core_) {
      core_->close();
    }
  }

  // Move-only semantics: owned exclusively by the consumer (popper).
  AsyncQueue(AsyncQueue&& other) noexcept = default;
  AsyncQueue& operator=(AsyncQueue&& other) noexcept = default;
  AsyncQueue(const AsyncQueue&) = delete;
  AsyncQueue& operator=(const AsyncQueue&) = delete;

  PushAccessor pushAccessor() const { return PushAccessor(core_); }

  uint64_t itemCount() const { return core_->itemCount(); }
  uint64_t currentSize() const { return core_->currentSize(); }
  std::optional<uint64_t> maxSize() const { return core_->maxSize(); }
  bool closed() const { return core_->closed(); }
  bool empty() const { return core_->empty(); }
  CapacityPtr capacity() const { return core_->capacity(); }

  Task<absl::Status> push(T item) { return core_->push(std::move(item)); }

  template <typename U = T> bool tryPush(U&& item) { return core_->tryPush(std::forward<U>(item)); }

  PopAwaitable pop() { return PopAwaitable(core_); }
  std::optional<T> tryPop() { return core_->tryPop(); }

  void close() { core_->close(); }

private:
  std::shared_ptr<Core> core_;
};

} // namespace Coroutine
} // namespace Envoy
