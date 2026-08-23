#include "source/common/coroutine/async_event.h"

namespace Envoy {
namespace Coroutine {

void AsyncEvent::EventAwaitable::onStart() {
  auto cb = [this]() { this->complete(absl::OkStatus()); };
  if (at_front_) {
    event_.waiters_.push_front(Waiter{std::move(cb), active_});
  } else {
    event_.waiters_.push_back(Waiter{std::move(cb), active_});
  }
}

void AsyncEvent::EventAwaitable::onCancel() { *active_ = false; }

void AsyncEvent::notifyOne() {
  while (!waiters_.empty()) {
    auto waiter = std::move(waiters_.front());
    waiters_.pop_front();
    if (*waiter.active && waiter.cb) {
      waiter.cb();
      break;
    }
  }
}

void AsyncEvent::notifyAll() {
  std::list<Waiter> batch;
  batch.swap(waiters_);

  while (!batch.empty()) {
    auto waiter = std::move(batch.front());
    batch.pop_front();
    if (*waiter.active && waiter.cb) {
      waiter.cb();
    }
  }
}

bool AsyncEvent::hasWaiters() const {
  for (const auto& waiter : waiters_) {
    if (*waiter.active) {
      return true;
    }
  }
  return false;
}

} // namespace Coroutine
} // namespace Envoy
