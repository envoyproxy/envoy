#pragma once

#include <atomic>

#include "envoy/server/watchdog.h"

namespace Envoy {
namespace Server {

/**
 * This class stores the actual data about when the WatchDog was last touched
 * along with thread metadata.
 */
class WatchDogImpl : public WatchDog {
public:
  /**
   * @param thread_id ThreadId of the monitored thread
   */
  WatchDogImpl(Thread::ThreadId thread_id) : thread_id_(thread_id) {}

  Thread::ThreadId threadId() const override { return thread_id_; }
  // Used by GuardDogImpl determine if the watchdog was touched recently and reset the touch status.
  bool getTouchedAndReset() { return touched_.exchange(false, std::memory_order_relaxed); }

  // Server::WatchDog
  void touch() override {
    // Set touched_ if not already set.
    bool expected = false;
    touched_.compare_exchange_strong(expected, true, std::memory_order_relaxed);
  }

private:
  const Thread::ThreadId thread_id_;
  std::atomic<bool> touched_{false};
};

} // namespace Server
} // namespace Envoy
