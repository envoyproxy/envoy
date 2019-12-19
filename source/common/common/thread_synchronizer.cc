#include "common/common/thread_synchronizer.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Thread {

ThreadSynchronizer::~ThreadSynchronizer() {
  if (data_ != nullptr) {
    // Make sure we don't have any pending signals which would indicate a bad test.
    for (auto& entry : data_->entries_) {
      ASSERT(!entry.second->signaled_);
    }
  }
}

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

void ThreadSynchronizer::waitWorker(absl::string_view event_name) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);

  // See if we are ignoring waits. If so, just return.
  if (entry.ignore_wait_) {
    ENVOY_LOG(debug, "thread synchronizer: waiting for {}: ignoring", event_name);
    return;
  }

  // See if we are already signaled. If so, just clear signaled and return.
  if (entry.signaled_) {
    ENVOY_LOG(debug, "thread synchronizer: waiting for {}: already signaled", event_name);
    entry.signaled_ = false;
    return;
  }

  // Now signal any barrier waiters.
  entry.at_barrier_ = true;

  // Now wait to be signaled.
  ENVOY_LOG(debug, "thread synchronizer: waiting for {}", event_name);
  entry.mutex_.Await(absl::Condition(&entry.signaled_));
  ENVOY_LOG(debug, "thread synchronizer: done waiting for {}", event_name);

  // Clear the barrier and signaled before unlocking and returning.
  ASSERT(entry.at_barrier_);
  entry.at_barrier_ = false;
  ASSERT(entry.signaled_);
  entry.signaled_ = false;
}

void ThreadSynchronizer::ignoreWaitWorker(absl::string_view event_name, bool ignore) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);
  ENVOY_LOG(debug, "thread synchronizer: update ignore wait for {}: {}", event_name, ignore);
  entry.ignore_wait_ = ignore;
}

void ThreadSynchronizer::barrierWorker(absl::string_view event_name) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);
  ENVOY_LOG(debug, "thread synchronizer: barrier at {}", event_name);
  entry.mutex_.Await(absl::Condition(&entry.at_barrier_));
  ENVOY_LOG(debug, "thread synchronizer: barrier complete {}", event_name);
}

void ThreadSynchronizer::signalWorker(absl::string_view event_name) {
  SynchronizerEntry& entry = getOrCreateEntry(event_name);
  absl::MutexLock lock(&entry.mutex_);
  ENVOY_LOG(debug, "thread synchronizer: signaling {}", event_name);
  ASSERT(!entry.signaled_);
  entry.signaled_ = true;
}

} // namespace Thread
} // namespace Envoy
