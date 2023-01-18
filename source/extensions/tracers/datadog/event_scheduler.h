#pragma once

#include <datadog/event_scheduler.h>

#include <chrono>
#include <functional>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

class EventScheduler : public datadog::tracing::EventScheduler {
public:
  explicit EventScheduler(Event::Dispatcher& dispatcher);
  ~EventScheduler() override;

  // datadog::tracing::EventScheduler

  // Repeatedly execute the specified `callback` with approximately `interval` between each
  // invocation, starting after an initial `interval`. Return a function that cancels future
  // invocations. If the returned function is invoked after this `EventScheduler` is destroyed, the
  // behavior is undefined.
  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override;

  nlohmann::json config_json() const override;

private:
  Event::Dispatcher* dispatcher_;
  std::vector<Event::TimerPtr> timers_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
