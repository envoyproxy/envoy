#include "common/config/pausable_ack_queue.h"

#include <list>

#include "common/common/assert.h"

namespace Envoy {
namespace Config {

void PausableAckQueue::push(UpdateAck x) { storage_.push_back(std::move(x)); }

size_t PausableAckQueue::size() const { return storage_.size(); }

bool PausableAckQueue::empty() {
  for (const auto& entry : storage_) {
    if (!paused_[entry.type_url_]) {
      return false;
    }
  }
  return true;
}

const UpdateAck& PausableAckQueue::front() {
  for (const auto& entry : storage_) {
    if (!paused_[entry.type_url_]) {
      return entry;
    }
  }
  RELEASE_ASSERT(false, "front() on an empty queue is undefined behavior!");
  NOT_REACHED_GCOVR_EXCL_LINE;
}

UpdateAck PausableAckQueue::popFront() {
  for (auto it = storage_.begin(); it != storage_.end(); ++it) {
    if (!paused_[it->type_url_]) {
      UpdateAck ret = *it;
      storage_.erase(it);
      return ret;
    }
  }
  RELEASE_ASSERT(false, "popFront() on an empty queue is undefined behavior!");
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void PausableAckQueue::pause(const std::string& type_url) {
  // It's ok to pause a subscription that doesn't exist yet.
  auto& pause_entry = paused_[type_url];
  ASSERT(!pause_entry);
  pause_entry = true;
}

void PausableAckQueue::resume(const std::string& type_url) {
  auto& pause_entry = paused_[type_url];
  ASSERT(pause_entry);
  pause_entry = false;
}

bool PausableAckQueue::paused(const std::string& type_url) const {
  auto entry = paused_.find(type_url);
  if (entry == paused_.end()) {
    return false;
  }
  return entry->second;
}

} // namespace Config
} // namespace Envoy
