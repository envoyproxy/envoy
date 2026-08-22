#include "source/common/coroutine/async_queue.h"

#include <algorithm>
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

void SharedCapacity::acquire(uint64_t size) { current_size_ += size; }

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
  if (processing_waiters_) {
    return;
  }
  processing_waiters_ = true;
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
