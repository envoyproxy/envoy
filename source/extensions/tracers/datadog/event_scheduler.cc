#include "source/extensions/tracers/datadog/event_scheduler.h"

#include <cassert>
#include <datadog/json.hpp>
#include <memory>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

EventScheduler::EventScheduler(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

EventScheduler::~EventScheduler() {
  for (const auto& timer : timers_) {
    timer->disableTimer();
  }
}

EventScheduler::Cancel
EventScheduler::schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                         std::function<void()> callback) {
  // Yes, a shared pointer to a pointer.
  auto self = std::make_shared<Event::Timer*>();

  Event::TimerPtr timer = dispatcher_.createTimer([self, interval, callback = std::move(callback)] {
    (**self).enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(interval));
    callback();
  });

  Event::Timer* timer_raw = timer.get();
  *self = timer_raw;

  timers_.emplace_back(std::move(timer));

  timer_raw->enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(interval));

  return [this, timer = timer_raw]() mutable {
    if (!timer) {
      return; // idempotent
    }

    timer->disableTimer();
    auto found = std::find_if(timers_.begin(), timers_.end(),
                              [&](const auto& element) { return element.get() == timer; });
    assert(found != timers_.end());
    timers_.erase(found);
    timer = nullptr;
  };
}

nlohmann::json EventScheduler::config_json() const {
  return nlohmann::json::object({
      {"type", "Envoy::Extensions::Tracers::Datadog::EventScheduler"},
  });
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
