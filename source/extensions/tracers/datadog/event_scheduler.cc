#include "source/extensions/tracers/datadog/event_scheduler.h"

#include <memory>
#include <utility>

#include "source/common/common/assert.h"

#include "datadog/json.hpp"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

EventScheduler::EventScheduler(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

EventScheduler::Cancel
EventScheduler::schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                         std::function<void()> callback) {
  auto self = std::make_shared<Event::Timer*>();
  // Yes, a shared pointer to a pointer.
  //
  // Both the timer callback (argument to `createTimer`, below) and the returned
  // cancellation function need a handle to the `Event::Timer`. The timer
  // callback needs it so that it can reschedule the next round when the timer
  // fires (that's how this is a _recurring_ event). The cancellation function
  // needs it so that it can call `disableTimer` and remove the timer from
  // `timers_`.
  //
  // Since we don't have a handle to the `Event::Timer` until `createTimer`
  // returns, we need a "box" out of which the timer callback can extract the
  // created timer. We then put the `Event::Timer*` in the "box" after
  // `createTimer` returns the timer. `self` is the "box."
  //
  // The cancellation function returned by this function refers to the
  // `Event::Timer` via a raw pointer (as does the timer callback, indirectly).
  // The actual lifetime of the pointed-to `Event::Timer` is determined by its
  // presence in `timers_`. The `Event::TimerPtr` returned by `createTimer` is
  // moved into `timers_`.

  Event::TimerPtr timer = dispatcher_.createTimer([self, interval, callback = std::move(callback)] {
    (**self).enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(interval));
    callback();
  });

  Event::Timer* timer_raw = timer.get();
  *self = timer_raw;

  timers_.insert(std::move(timer));

  timer_raw->enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(interval));

  return [this, timer = timer_raw]() mutable {
    if (!timer) {
      return; // idempotent
    }

    timer->disableTimer();
    auto found = timers_.find(timer);
    RELEASE_ASSERT(found != timers_.end(),
                   "timer not found in registry of timers in Datadog::EventScheduler");
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
