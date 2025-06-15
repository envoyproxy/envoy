#include <chrono>
#include <string>

#include "envoy/event/dispatcher.h"

#include "source/extensions/tracers/datadog/event_scheduler.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

using testing::NiceMock;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

// Test class to verify Datadog EventScheduler behaviors
class DatadogEventSchedulerTest : public testing::Test {
public:
  DatadogEventSchedulerTest()
      : thread_local_storage_(std::make_shared<NiceMock<ThreadLocal::MockInstance>>()),
        scheduler_(thread_local_storage_->dispatcher_) {}

protected:
  std::shared_ptr<NiceMock<ThreadLocal::MockInstance>> thread_local_storage_;
  EventScheduler scheduler_;
};

// Verify that the config() method produces a valid string that can be parsed as JSON
TEST_F(DatadogEventSchedulerTest, ConfigJson) {
  const std::string config = scheduler_.config();

  // Verify it's not empty
  EXPECT_FALSE(config.empty());

  // Parse the config string to verify it's valid JSON
  EXPECT_NO_THROW({
    auto json = nlohmann::json::parse(config);
    EXPECT_TRUE(json.is_object());
    EXPECT_EQ("Envoy::Extensions::Tracers::Datadog::EventScheduler", json["type"]);
  });
}

// Test config_json returns expected content
TEST_F(DatadogEventSchedulerTest, ConfigJsonMethod) {
  nlohmann::json config = scheduler_.config_json();
  EXPECT_EQ("Envoy::Extensions::Tracers::Datadog::EventScheduler", config["type"]);
}

// Test that the scheduler creates a timer when scheduling an event
TEST_F(DatadogEventSchedulerTest, ScheduleRecurringEventCallsCreatesATimer) {
  testing::MockFunction<void()> callback;
  // The interval is arbitrary in these tests; we just have to be able to
  // compare it to what was passed to the mocks.
  // The only requirement is that it be divisible by milliseconds, because
  // that's what `Timer::enableTimer` accepts.
  const std::chrono::milliseconds interval(2000);

  EXPECT_CALL(thread_local_storage_->dispatcher_, createTimer_(_));

  scheduler_.schedule_recurring_event(interval, callback.AsStdFunction());
}

// This could be tested above, but introducing an `Event::MockTimer` disrupts
// our ability to track calls to `MockDispatcher::createTimer_`. So, two
// separate tests.
TEST_F(DatadogEventSchedulerTest, ScheduleRecurringEventEnablesATimer) {
  auto* const timer = new NiceMock<Event::MockTimer>(&thread_local_storage_->dispatcher_);
  testing::MockFunction<void()> callback;
  const std::chrono::milliseconds interval(2000);

  EXPECT_CALL(*timer, enableTimer(interval, _));

  scheduler_.schedule_recurring_event(interval, callback.AsStdFunction());
}

// Test that the timer's callback properly invokes the user-supplied callback and reschedules
TEST_F(DatadogEventSchedulerTest, TriggeredTimerInvokesCallbackAndReschedulesItself) {
  auto* const timer = new NiceMock<Event::MockTimer>(&thread_local_storage_->dispatcher_);
  testing::MockFunction<void()> callback;
  const std::chrono::milliseconds interval(2000);

  // Once for the initial round, and then again when the callback is invoked.
  EXPECT_CALL(*timer, enableTimer(interval, _)).Times(2);
  // The user-supplied callback is called once when the timer triggers.
  EXPECT_CALL(callback, Call());

  scheduler_.schedule_recurring_event(interval, callback.AsStdFunction());
  timer->invokeCallback();
}

// Test that the cancellation function properly disables the timer
TEST_F(DatadogEventSchedulerTest, CancellationFunctionCallsDisableTimerOnce) {
  auto* const timer = new NiceMock<Event::MockTimer>(&thread_local_storage_->dispatcher_);
  testing::MockFunction<void()> callback;
  const std::chrono::milliseconds interval(2000);

  EXPECT_CALL(*timer, disableTimer());

  const auto cancel = scheduler_.schedule_recurring_event(interval, callback.AsStdFunction());
  cancel();
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
  cancel(); // idempotent
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
