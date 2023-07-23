#include "source/common/common/thread_synchronizer.h"

namespace Envoy {
namespace Thread {

void ThreadSynchronizer::enable() {
  ASSERT(data_ == nullptr);
  data_ = std::make_unique<SynchronizerData>();
}

ThreadSynchronizer::SynchronizerEntry&
ThreadSynchronizer::getOrCreateEntry(absl::string_view event_name) {
  absl::MutexLock lock(&data_->mutex_);
  auto& existing_entry = data_->entries_[event_name];
  if (existing_entry == nullptr) {
    ENVOY_LOG(debug, "thread synchronzier: creating entry: {}", event_name);
    existing_entry = std::make_unique<SynchronizerEntry>();
  }
  return *existing_entry;
}

void ThreadSynchronizer::waitOnWorker(absl::string_view event_name) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);
  ENVOY_LOG(debug, "thread synchronizer: waiting on next {}", event_name);
  ASSERT(!entry.wait_on_);
  entry.wait_on_ = true;
}

void ThreadSynchronizer::syncPointWorker(absl::string_view event_name) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);

  // See if we are ignoring waits. If so, just return.
  if (!entry.wait_on_) {
    ENVOY_LOG(debug, "thread synchronizer: sync point {}: ignoring", event_name);
    return;
  }
  entry.wait_on_ = false;

  // See if we are already signaled. If so, just clear signaled and return.
  if (entry.signaled_) {
    ENVOY_LOG(debug, "thread synchronizer: sync point {}: already signaled", event_name);
    entry.signaled_ = false;
    return;
  }

  // Now signal any barrier waiters.
  entry.at_barrier_ = true;

  // Now wait to be signaled.
  ENVOY_LOG(debug, "thread synchronizer: blocking on sync point {}", event_name);
  entry.mutex_.Await(absl::Condition(&entry.signaled_));
  ENVOY_LOG(debug, "thread synchronizer: done blocking for sync point {}", event_name);

  // Clear the barrier and signaled before unlocking and returning.
  ASSERT(entry.at_barrier_);
  entry.at_barrier_ = false;
  ASSERT(entry.signaled_);
  entry.signaled_ = false;
}

void ThreadSynchronizer::barrierOnWorker(absl::string_view event_name) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);
  ENVOY_LOG(debug, "thread synchronizer: barrier on {}", event_name);
  while (!entry.mutex_.AwaitWithTimeout(absl::Condition(&entry.at_barrier_), absl::Seconds(10))) {
    ENVOY_LOG(warn, "thread synchronizer: barrier on {} stuck for 10 seconds", event_name);
  }
  ENVOY_LOG(debug, "thread synchronizer: barrier complete {}", event_name);
}

void ThreadSynchronizer::signalWorker(absl::string_view event_name) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);
  ASSERT(!entry.signaled_);
  ENVOY_LOG(debug, "thread synchronizer: signaling {}", event_name);
  entry.signaled_ = true;
}

} // namespace Thread
} // namespace Envoy
