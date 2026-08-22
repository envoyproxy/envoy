#include "source/common/coroutine/async_queue.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Coroutine {

SharedCapacity::SharedCapacity(std::optional<uint64_t> max_size) : max_size_(max_size) {}

SharedCapacity::~SharedCapacity() { *alive_ = false; }

bool SharedCapacity::hasCapacity(uint64_t additional_size) const {
  if (!max_size_.has_value()) {
    return true;
  }
  if (current_size_ > *max_size_) {
    return false;
  }
  return additional_size <= (*max_size_ - current_size_);
}

bool SharedCapacity::canAcquire(uint64_t size) const {
  return hasCapacity(size) || current_size_ == 0;
}

void SharedCapacity::acquire(uint64_t size) {
  // Defense-in-depth: assert against integer overflow when adding to current_size_.
  // This is mathematically prevented by hasCapacity() and canAcquire(), but asserted
  // here to safeguard against arithmetic wrap-around.
  ASSERT(std::numeric_limits<uint64_t>::max() - current_size_ >= size);
  current_size_ += size;
}

void SharedCapacity::release(uint64_t size) {
  ASSERT(current_size_ >= size);
  current_size_ -= size;
  processWaiters();
}

bool SharedCapacity::tryAcquire(uint64_t size) {
  if (!waiters_.empty()) {
    return false;
  }
  if (!canAcquire(size)) {
    return false;
  }
  acquire(size);
  return true;
}

SharedCapacity::WaiterIterator SharedCapacity::requestCapacity(uint64_t size, GrantCallback cb) {
  ASSERT(cb != nullptr);
  // Defensive fallback: if an invalid callback is passed in release builds, return
  // waiters_.end() without enqueuing. This ensures:
  // 1. processWaiters() will never encounter or attempt to invoke a null callback.
  // 2. cancelRequest(waiters_.end()) safely early-exits as a no-op if the caller
  //    subsequently attempts cancellation on the returned iterator.
  if (!cb) {
    return waiters_.end();
  }
  return waiters_.insert(waiters_.end(), Waiter{size, std::move(cb)});
}

void SharedCapacity::cancelRequest(WaiterIterator it) {
  if (it == waiters_.end()) {
    return;
  }
  bool is_head = (it == waiters_.begin());
  waiters_.erase(it);
  // If the cancelled waiter was at the head of the wait queue, it may have been
  // blocking subsequent waiters that require smaller capacity. Re-evaluate the
  // new head of the queue immediately.
  if (is_head) {
    processWaiters();
  }
}

void SharedCapacity::processWaiters() {
  // Reentrancy protection: grant callbacks invoked by processWaiters() may synchronously
  // call tryAcquire(), release(), or cancelRequest() on this SharedCapacity instance.
  // Setting processing_waiters_ = true prevents nested reentrant loops from processing
  // the waiters list concurrently. Any mutations to the waiters list or capacity made
  // during a callback will be naturally picked up by the outer while-loop.
  if (processing_waiters_) {
    return;
  }
  processing_waiters_ = true;

  // Capture a shared reference to alive_ before executing callbacks. If a callback
  // synchronously destroys this SharedCapacity instance, *alive will become false,
  // allowing us to exit safely without touching any member variables.
  auto alive = alive_;

  while (!waiters_.empty()) {
    Waiter& head = waiters_.front();
    if (!canAcquire(head.size)) {
      break;
    }

    uint64_t grant_size = head.size;
    GrantCallback cb = std::move(head.cb);
    waiters_.pop_front();

    ASSERT(cb != nullptr);
    if (!cb) {
      continue;
    }

    acquire(grant_size);
    cb();

    if (!*alive) {
      return;
    }
  }

  processing_waiters_ = false;
}

} // namespace Coroutine
} // namespace Envoy
