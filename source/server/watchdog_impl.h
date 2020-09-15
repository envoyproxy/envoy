#pragma once

#include <atomic>
#include <chrono>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
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
   * @param interval WatchDog timer interval (used after startWatchdog())
   */
  WatchDogImpl(Thread::ThreadId thread_id, std::chrono::milliseconds interval)
      : thread_id_(thread_id), timer_interval_(interval) {}

  Thread::ThreadId threadId() const override { return thread_id_; }
  uint64_t touchCount() const { return touch_count_.load(); }

  // Server::WatchDog
  void startWatchdog(Event::Dispatcher& dispatcher) override;
  void touch() override { touch_count_.fetch_add(1); }

private:
  const Thread::ThreadId thread_id_;
  std::atomic<uint64_t> touch_count_ = 0;
  Event::TimerPtr timer_;
  const std::chrono::milliseconds timer_interval_;
};

} // namespace Server
} // namespace Envoy
