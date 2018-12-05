#pragma once

#include <atomic>
#include <chrono>

#include "envoy/api/api.h"
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
  WatchDogImpl(TimeSource& tsource, std::chrono::milliseconds interval, Api::Api& api)
      : thread_id_(api.threadFactory().currentThreadId()), time_source_(tsource),
        latest_touch_time_since_epoch_(tsource.monotonicTime().time_since_epoch()),
        timer_interval_(interval) {}

  std::string threadId() const override { return thread_id_->debugString(); }
  MonotonicTime lastTouchTime() const override {
    return MonotonicTime(latest_touch_time_since_epoch_.load());
  }

  // Server::WatchDog
  void startWatchdog(Event::Dispatcher& dispatcher) override;
  void touch() override {
    latest_touch_time_since_epoch_.store(time_source_.monotonicTime().time_since_epoch());
  }

private:
  Thread::ThreadIdPtr thread_id_;
  TimeSource& time_source_;
  std::atomic<std::chrono::steady_clock::duration> latest_touch_time_since_epoch_;
  Event::TimerPtr timer_;
  const std::chrono::milliseconds timer_interval_;
};

} // namespace Server
} // namespace Envoy
