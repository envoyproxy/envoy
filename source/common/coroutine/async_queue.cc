#include "source/common/coroutine/async_queue.h"

namespace Envoy {
namespace Coroutine {

SharedCapacity::SharedCapacity(std::optional<uint64_t> max_size) : max_size_(max_size) {
  if (max_size_.has_value()) {
    ASSERT(*max_size_ > 0, "max_size must be positive if specified");
  }
}

SharedCapacity::~SharedCapacity() {
  *alive_ = false;
  waiters_.clear();
  capacity_event_.notifyAll();
}

bool SharedCapacity::hasCapacity(uint64_t additional_size) const {
  if (!max_size_.has_value()) {
    return true;
  }
  if (current_size_ > *max_size_) {
    return false;
  }
  if (additional_size > std::numeric_limits<uint64_t>::max() - current_size_) {
    return false;
  }
  return (current_size_ + additional_size) <= *max_size_;
}

bool SharedCapacity::canAcquire(uint64_t size) const {
  if (!max_size_.has_value()) {
    return true;
  }
  if (current_size_ == 0 && size > *max_size_) {
    return true;
  }
  return hasCapacity(size);
}

bool SharedCapacity::tryAcquire(uint64_t size) {
  if (waiters_.empty() && canAcquire(size)) {
    current_size_ += size;
    return true;
  }
  return false;
}

Task<absl::Status> SharedCapacity::acquire(uint64_t size) {
  if (waiters_.empty() && canAcquire(size)) {
    current_size_ += size;
    co_return absl::OkStatus();
  }

  auto alive = alive_;
  auto it = waiters_.insert(waiters_.end(), size);
  auto cleanup = absl::MakeCleanup([this, alive, it]() {
    if (*alive) {
      const bool was_head = (it == waiters_.begin());
      waiters_.erase(it);
      if (was_head) {
        capacity_event_.notifyOne();
      }
    }
  });

  bool first_wait = true;
  while (it != waiters_.begin() || !canAcquire(size)) {
    auto status = co_await (first_wait ? capacity_event_.wait() : capacity_event_.waitFront());
    first_wait = false;
    if (!status.ok()) {
      co_return status;
    }
    if (!*alive) {
      co_return absl::FailedPreconditionError("SharedCapacity is destroyed");
    }
  }

  std::move(cleanup).Cancel();
  waiters_.erase(it);
  current_size_ += size;

  capacity_event_.notifyOne();
  co_return absl::OkStatus();
}

void SharedCapacity::release(uint64_t size) {
  if (size == 0) {
    return;
  }
  ASSERT(current_size_ >= size);
  if (current_size_ < size) {
    current_size_ = 0;
  } else {
    current_size_ -= size;
  }
  capacity_event_.notifyOne();
}

} // namespace Coroutine
} // namespace Envoy
