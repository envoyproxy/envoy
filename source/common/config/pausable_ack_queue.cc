#include "common/config/pausable_ack_queue.h"

#include <list>

#include "common/common/assert.h"

namespace Envoy {
namespace Config {

void PausableAckQueue::push(UpdateAck x) { storage_.push_back(std::move(x)); }

size_t PausableAckQueue::size() const { return storage_.size(); }

bool PausableAckQueue::empty() {
  for (const auto& entry : storage_) {
    if (pauses_[entry.type_url_] == 0) {
      return false;
    }
  }
  return true;
}

const UpdateAck& PausableAckQueue::front() {
  for (const auto& entry : storage_) {
    if (pauses_[entry.type_url_] == 0) {
      return entry;
    }
  }
  RELEASE_ASSERT(false, "front() on an empty queue is undefined behavior!");
  NOT_REACHED_GCOVR_EXCL_LINE;
}

UpdateAck PausableAckQueue::popFront() {
  for (auto it = storage_.begin(); it != storage_.end(); ++it) {
    if (pauses_[it->type_url_] == 0) {
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
  auto& pause_entry = pauses_[type_url];
  ++pause_entry;
}

void PausableAckQueue::resume(const std::string& type_url) {
  auto& pause_entry = pauses_[type_url];
  ASSERT(pause_entry > 0);
  --pause_entry;
}

bool PausableAckQueue::paused(const std::string& type_url) const {
  auto entry = pauses_.find(type_url);
  if (entry == pauses_.end()) {
    return false;
  }
  return entry->second > 0;
}

} // namespace Config
} // namespace Envoy
