#include "source/extensions/config_subscription/grpc/pausable_ack_queue.h"

#include <list>

#include "source/common/common/assert.h"

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

// In the event of a reconnection, clear all the cached nonces.
void PausableAckQueue::clear() { storage_.clear(); }

const UpdateAck& PausableAckQueue::front() {
  for (const auto& entry : storage_) {
    if (pauses_[entry.type_url_] == 0) {
      return entry;
    }
  }
  PANIC("front() on an empty queue is undefined behavior!");
}

UpdateAck PausableAckQueue::popFront() {
  for (auto it = storage_.begin(); it != storage_.end(); ++it) {
    if (pauses_[it->type_url_] == 0) {
      UpdateAck ret = *it;
      storage_.erase(it);
      return ret;
    }
  }
  PANIC("popFront() on an empty queue is undefined behavior!");
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
