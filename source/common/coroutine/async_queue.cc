#include "source/common/coroutine/async_queue.h"

#include <algorithm>
#include <utility>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Coroutine {

SharedCapacity::SharedCapacity(std::optional<uint64_t> max_size) : max_size_(max_size) {}

SharedCapacity::~SharedCapacity() = default;

bool SharedCapacity::hasCapacity(uint64_t additional_size) const {
  if (!max_size_.has_value()) {
    return true;
  }
  return current_size_ + additional_size <= *max_size_;
}

void SharedCapacity::acquire(uint64_t size) { current_size_ += size; }

void SharedCapacity::release(uint64_t size) {
  ASSERT(current_size_ >= size);
  current_size_ -= size;
  if (!notifying_) {
    notifySpaceAvailable();
  }
}

uint64_t SharedCapacity::addSpaceAvailableCallback(SpaceCallback cb) {
  uint64_t id = ++next_callback_id_;
  callbacks_.push_back(CallbackEntry{std::move(cb), id, false});
  return id;
}

void SharedCapacity::removeSpaceAvailableCallback(uint64_t id) {
  for (CallbackEntry& entry : callbacks_) {
    if (entry.id == id) {
      entry.cb = nullptr;
      entry.removed = true;
      break;
    }
  }
  if (!notifying_) {
    cleanupRemoved();
  }
}

void SharedCapacity::notifySpaceAvailable() {
  if (notifying_) {
    return;
  }
  notifying_ = true;
  for (size_t i = 0; i < callbacks_.size(); ++i) {
    if (max_size_.has_value() && current_size_ >= *max_size_) {
      break;
    }
    if (!callbacks_[i].removed && callbacks_[i].cb) {
      callbacks_[i].cb();
    }
  }
  notifying_ = false;
  cleanupRemoved();
}

void SharedCapacity::cleanupRemoved() {
  callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
                                  [](const CallbackEntry& e) { return e.removed; }),
                   callbacks_.end());
}

} // namespace Coroutine
} // namespace Envoy
