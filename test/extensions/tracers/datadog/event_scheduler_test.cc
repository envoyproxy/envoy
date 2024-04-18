#include <chrono>

#include "source/extensions/tracers/datadog/event_scheduler.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "datadog/json.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

TEST(DatadogEventSchedulerTest, ScheduleRecurringEventCallsCreatesATimer) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  // The interval is arbitrary in these tests; we just have to be able to
  // compare it to what was passed to the mocks.
  // The only requirement is that it be divisible by milliseconds, because
  // that's what `Timer::enableTimer` accepts.
  const std::chrono::milliseconds interval(2000);

  EXPECT_CALL(thread_local_storage_.dispatcher_, createTimer_(testing::_));

  scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
}

// This could be tested above, but introducing an `Event::MockTimer` disrupts
// our ability to track calls to `MockDispatcher::createTimer_`. So, two
// separate tests.
TEST(DatadogEventSchedulerTest, ScheduleRecurringEventEnablesATimer) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  auto* const timer = new testing::NiceMock<Event::MockTimer>(&thread_local_storage_.dispatcher_);

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  const std::chrono::milliseconds interval(2000);

  EXPECT_CALL(*timer, enableTimer(interval, testing::_));

  scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
}

TEST(DatadogEventSchedulerTest, TriggeredTimerInvokesCallbackAndReschedulesItself) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  auto* const timer = new testing::NiceMock<Event::MockTimer>(&thread_local_storage_.dispatcher_);

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  const std::chrono::milliseconds interval(2000);

  // Once for the initial round, and then again when the callback is invoked.
  EXPECT_CALL(*timer, enableTimer(interval, testing::_)).Times(2);
  // The user-supplied callback is called once when the timer triggers.
  EXPECT_CALL(callback, Call());

  scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
  timer->invokeCallback();
}

TEST(DatadogEventSchedulerTest, CancellationFunctionCallsDisableTimerOnce) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  auto* const timer = new testing::NiceMock<Event::MockTimer>(&thread_local_storage_.dispatcher_);

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  const std::chrono::milliseconds interval(2000);

  EXPECT_CALL(*timer, disableTimer());

  const auto cancel = scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
  cancel();
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
}

TEST(DatadogEventSchedulerTest, ConfigJson) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  nlohmann::json config = scheduler.config_json();
  EXPECT_EQ("Envoy::Extensions::Tracers::Datadog::EventScheduler", config["type"]);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
