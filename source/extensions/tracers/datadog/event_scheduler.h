#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "absl/container/flat_hash_set.h"
#include "datadog/event_scheduler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

/**
 * Registry of recurring events (timers). This class implements dd-trace-cpp's
 * \c datadog::tracing::EventScheduler interface in terms of an
 * \c Event::Dispatcher, which is what is used by Envoy. An instance of this class
 * is passed into dd-trace-cpp when tracing is configured, allowing dd-trace-cpp
 * to periodically send batches of traces to the Datadog Agent.
 */
class EventScheduler : public datadog::tracing::EventScheduler {
public:
  explicit EventScheduler(Event::Dispatcher& dispatcher);

  /**
   * Repeatedly execute the specified \p callback with approximately \p interval
   * between each invocation, starting after an initial \p interval. Return a
   * function that cancels future invocations. If the returned function is
   * invoked after this \c EventScheduler is destroyed, the behavior is
   * undefined.
   * @param interval how often the event will occur
   * @param callback the function invoked when the event occurs
   * @return a zero-parameter function that cancels the recurring event
   */
  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override;

  nlohmann::json config_json() const override;

private:
  Event::Dispatcher& dispatcher_;
  absl::flat_hash_set<Event::TimerPtr> timers_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
