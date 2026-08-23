#pragma once

#include <list>
#include <memory>
#include <utility>

#include "source/common/coroutine/leaf_awaitable.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Coroutine {

/**
 * AsyncEvent provides a lightweight asynchronous notification primitive.
 * Coroutines can await an event, and producers can wake one or all waiting coroutines.
 */
class AsyncEvent {
public:
  class EventAwaitable : public LeafAwaitable<absl::Status> {
  public:
    explicit EventAwaitable(AsyncEvent& event, bool at_front = false)
        : event_(event), at_front_(at_front) {}

  protected:
    void onStart() override;
    void onCancel() override;

  private:
    AsyncEvent& event_;
    bool at_front_{false};
    std::shared_ptr<bool> active_{std::make_shared<bool>(true)};
  };

  AsyncEvent() = default;
  ~AsyncEvent() = default;

  AsyncEvent(const AsyncEvent&) = delete;
  AsyncEvent& operator=(const AsyncEvent&) = delete;
  AsyncEvent(AsyncEvent&&) = delete;
  AsyncEvent& operator=(AsyncEvent&&) = delete;

  /**
   * Suspends the calling coroutine and registers it at the back of the waiter queue (FIFO).
   */
  EventAwaitable wait() { return EventAwaitable(*this, /*at_front=*/false); }

  /**
   * Suspends the calling coroutine and registers it at the front of the waiter queue (LIFO).
   *
   * Caveat: If multiple waiters call waitFront() during a notifyAll() pass, each successive
   * waiter is prepended to the queue, which effectively reverses their relative order for the next
   * pass. This is primarily intended for single head-waiter re-suspension (e.g. when the head
   * waiter in a semaphore cannot yet acquire sufficient capacity).
   */
  EventAwaitable waitFront() { return EventAwaitable(*this, /*at_front=*/true); }

  void notifyOne();
  void notifyAll();

  bool hasWaiters() const;

private:
  struct Waiter {
    absl::AnyInvocable<void()> cb;
    std::shared_ptr<bool> active;
  };

  std::list<Waiter> waiters_;
};

} // namespace Coroutine
} // namespace Envoy
