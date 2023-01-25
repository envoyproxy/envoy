#include "source/extensions/tracers/datadog/event_scheduler.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <chrono>

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
  const auto interval = std::chrono::milliseconds(2000);

  EXPECT_CALL(thread_local_storage_.dispatcher_, createTimer_(testing::_));

  (void)scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
}

// This could be tested above, but introducing an `Event::MockTimer` disrupts
// our ability to track calls to `MockDispatcher::createTimer_`. So, two
// separate tests.
TEST(DatadogEventSchedulerTest, ScheduleRecurringEventEnablesATimer) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  auto* const timer_ = new testing::NiceMock<Event::MockTimer>(&thread_local_storage_.dispatcher_);

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  const auto interval = std::chrono::milliseconds(2000);

  EXPECT_CALL(*timer_, enableTimer(interval, testing::_));

  (void)scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
}

TEST(DatadogEventSchedulerTest, TriggeredTimerInvokesCallbackAndReschedulesItself) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  auto* const timer_ = new testing::NiceMock<Event::MockTimer>(&thread_local_storage_.dispatcher_);

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  const auto interval = std::chrono::milliseconds(2000);

  // Once for the initial round, and then again when the callback is invoked.
  EXPECT_CALL(*timer_, enableTimer(interval, testing::_)).Times(2);
  // The user-supplied callback is called once when the timer triggers.
  EXPECT_CALL(callback, Call());

  (void)scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
  timer_->invokeCallback();
}

TEST(DatadogEventSchedulerTest, CancellationFunctionCallsDisableTimerOnce) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  auto* const timer_ = new testing::NiceMock<Event::MockTimer>(&thread_local_storage_.dispatcher_);

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  const auto interval = std::chrono::milliseconds(2000);

  EXPECT_CALL(*timer_, disableTimer());

  const auto cancel = scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
  cancel();
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
}

TEST(DatadogEventSchedulerTest, DestructorCallsDisableTimerIfCancelDidNot) {
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_storage_;
  auto* const timer_ = new testing::NiceMock<Event::MockTimer>(&thread_local_storage_.dispatcher_);

  EventScheduler scheduler{thread_local_storage_.dispatcher_};
  testing::MockFunction<void()> callback;
  const auto interval = std::chrono::milliseconds(2000);

  // when `scheduler` is destroyed
  EXPECT_CALL(*timer_, disableTimer());

  (void)scheduler.schedule_recurring_event(interval, callback.AsStdFunction());
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
