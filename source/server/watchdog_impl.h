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
   * @param thread_id A system thread ID (such as from Thread::currentThreadId())
   * @param interval WatchDog timer interval (used after startWatchdog())
   */
  WatchDogImpl(int32_t thread_id, MonotonicTimeSource& tsource, std::chrono::milliseconds interval)
      : thread_id_(thread_id), time_source_(tsource),
        latest_touch_time_since_epoch_(tsource.currentTime().time_since_epoch()),
        timer_interval_(interval) {}

  int32_t threadId() const override { return thread_id_; }
  MonotonicTime lastTouchTime() const override {
    return MonotonicTime(latest_touch_time_since_epoch_.load());
  }

  // Server::WatchDog
  void startWatchdog(Event::Dispatcher& dispatcher) override;
  void touch() override {
    latest_touch_time_since_epoch_.store(time_source_.currentTime().time_since_epoch());
  }

private:
  const int32_t thread_id_;
  MonotonicTimeSource& time_source_;
  std::atomic<std::chrono::steady_clock::duration> latest_touch_time_since_epoch_;
  Event::TimerPtr timer_;
  const std::chrono::milliseconds timer_interval_;
};

} // namespace Server
} // namespace Envoy
