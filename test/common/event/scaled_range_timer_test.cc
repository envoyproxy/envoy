#include <chrono>

#include "envoy/event/timer.h"

#include "common/event/scaled_range_timer.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace std {
namespace chrono {
void PrintTo(const std::chrono::milliseconds& ms, std::ostream* os) { (*os) << ms.count() << "ms"; }
void PrintTo(const std::chrono::microseconds& ms, std::ostream* os) { (*os) << ms.count() << "us"; }
void PrintTo(const std::chrono::nanoseconds& ms, std::ostream* os) { (*os) << ms.count() << "ns"; }
} // namespace chrono
} // namespace std

namespace Envoy {
namespace Event {
namespace {

using testing::_;
using testing::AllOf;
using testing::AnyNumber;
using testing::ByMove;
using testing::Contains;
using testing::DoAll;
using testing::Field;
using testing::Ge;
using testing::InSequence;
using testing::InvokeArgument;
using testing::Le;
using testing::Mock;
using testing::MockFunction;
using testing::NiceMock;
using testing::Pointee;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

// Test-only subclass of ScaledRangeTimerManager to expose some protected behavior for testing.
class TestScaledRangeTimerManager : public ScaledRangeTimerManager {
public:
  using ScaledRangeTimerManager::getBucketForDuration;
  using ScaledRangeTimerManager::ScaledRangeTimerManager;
  std::chrono::milliseconds getBucketedDuration(std::chrono::milliseconds input) {
    return getBucketDuration(getBucketForDuration(input));
  }
};

class ScaledRangeTimerManagerTest : public testing::Test {
public:
  ScaledRangeTimerManagerTest() {
    EXPECT_CALL(dispatcher_, createTimer_)
        .Times(AnyNumber())
        .WillRepeatedly([this](TimerCb callback) {
          auto* timer = new NiceMock<MockTimer>(callback);
          auto* enabled_time = new MonotonicTime(MonotonicTime::min());
          bucket_timers_.emplace_back(timer, std::unique_ptr<MonotonicTime>(enabled_time));
          ON_CALL(*timer, enableTimer)
              .WillByDefault([this, timer, enabled_time](const std::chrono::milliseconds& duration,
                                                         const ScopeTrackedObject* scope) {
                *enabled_time = dispatcher_.timeSource().monotonicTime();
                timer->enabled_ = true;
                timer->duration_ms_ = duration;
                timer->scope_ = scope;
              });
          return timer;
        });
    ON_CALL(dispatcher_, approximateMonotonicTime).WillByDefault([&] {
      return dispatcher_.timeSource().monotonicTime();
    });
  }

  SimulatedTimeSystem simulated_time;
  NiceMock<MockDispatcher> dispatcher_;
  std::vector<std::pair<MockTimer*, std::unique_ptr<MonotonicTime>>> bucket_timers_;
};

struct RangeTimerGroup {
  RangeTimerGroup(ScaledRangeTimerManager& manager, MockDispatcher& dispatcher)
      : callback(std::make_unique<MockFunction<void()>>()),
        pending_timer(new NiceMock<MockTimer>(&dispatcher)),
        timer(manager.createTimer(callback->AsStdFunction())) {}

  std::unique_ptr<MockFunction<void()>> callback;
  MockTimer* pending_timer;
  RangeTimerPtr timer;
};

template <typename T> bool fireReadyTimers(MonotonicTime now, T& timers) {
  bool fired = false;
  for (size_t i = 0; i < timers.size(); ++i) {
    auto& timer_group = timers[i];
    MockTimer* timer = std::get<MockTimer*>(timer_group);
    if (timer->enabled_ && timer->duration_ms_ + *timer_group.second <= now) {
      timer->invokeCallback();
      fired = true;
    }
  }
  return fired;
}

MATCHER_P2(InRange, low, high, "") {
  return testing::ExplainMatchResult(AllOf(Ge(low), Le(high)), arg, result_listener);
}

MATCHER_P(HasDeadline, matcher, "") {
  if (!arg.first->enabled_) {
    (*result_listener) << "not enabled";
    return false;
  }
  return testing::ExplainMatchResult(matcher, *arg.second + arg.first->duration_ms_,
                                     result_listener);
}

TEST_F(ScaledRangeTimerManagerTest, CreateAndDestroy) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);
}

TEST_F(ScaledRangeTimerManagerTest, CreateAndDestroyTimer) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);

  {
    RangeTimerGroup group(manager, dispatcher_);
    group.timer.reset();
  }
}

TEST_F(ScaledRangeTimerManagerTest, CreateSingleScaledTimer) {
  TestScaledRangeTimerManager manager(dispatcher_, 1.0);
  const std::chrono::milliseconds real_scaling_duration =
      manager.getBucketedDuration(std::chrono::seconds(5));
  EXPECT_THAT(real_scaling_duration,
              InRange(std::chrono::milliseconds(2500), std::chrono::milliseconds(5000)));

  RangeTimerGroup range_timer(manager, dispatcher_);
  EXPECT_CALL(*range_timer.pending_timer,
              enableTimer(std::chrono::seconds(10) - real_scaling_duration, _));

  EXPECT_CALL(*range_timer.callback, Call());

  range_timer.timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(10));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(range_timer.pending_timer->duration_ms_);
  range_timer.pending_timer->invokeCallback();
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(5));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
}

TEST_F(ScaledRangeTimerManagerTest, EnableAndDisableTimer) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);

  RangeTimerGroup range_timer(manager, dispatcher_);

  EXPECT_CALL(
      *range_timer.pending_timer,
      enableTimer(InRange(std::chrono::seconds(5),
                          std::chrono::milliseconds(17500) /* = 5 + (30 - 5) / 2 seconds */),
                  _));
  EXPECT_CALL(*range_timer.pending_timer, disableTimer);

  range_timer.timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(30));
  range_timer.timer->disableTimer();
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileDisabled) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);

  RangeTimerGroup timer(manager, dispatcher_);
  EXPECT_FALSE(timer.timer->enabled());
  EXPECT_FALSE(timer.pending_timer->enabled());

  timer.timer->disableTimer();
  EXPECT_FALSE(timer.timer->enabled());
  EXPECT_FALSE(timer.pending_timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhilePending) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);

  RangeTimerGroup timer(manager, dispatcher_);
  timer.timer->enableTimer(std::chrono::seconds(10), std::chrono::seconds(100));
  EXPECT_TRUE(timer.timer->enabled());
  EXPECT_TRUE(timer.pending_timer->enabled());

  timer.timer->disableTimer();
  EXPECT_FALSE(timer.timer->enabled());
  EXPECT_FALSE(timer.pending_timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileActive) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);

  RangeTimerGroup timer(manager, dispatcher_);

  timer.timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(100));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(5));

  timer.pending_timer->invokeCallback();
  EXPECT_TRUE(timer.timer->enabled());
  EXPECT_FALSE(timer.pending_timer->enabled());

  timer.timer->disableTimer();
  EXPECT_FALSE(timer.timer->enabled());
  EXPECT_FALSE(timer.pending_timer->enabled());
}

class ScaledRangeTimerManagerTestWithScope : public ScaledRangeTimerManagerTest,
                                             public testing::WithParamInterface<bool> {
public:
  ScopeTrackedObject* GetScope() { return GetParam() ? &scope_ : nullptr; }
  MockScopedTrackedObject scope_;
};

TEST_P(ScaledRangeTimerManagerTestWithScope, ReRegisterOnCallback) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);

  RangeTimerGroup timer(manager, dispatcher_);
  if (GetParam()) {
    InSequence s;
    EXPECT_CALL(dispatcher_, setTrackedObject(GetScope()));
    EXPECT_CALL(*timer.callback, Call).WillOnce([&] {
      timer.timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2), GetScope());
    });
    EXPECT_CALL(dispatcher_, setTrackedObject(nullptr));
    EXPECT_CALL(dispatcher_, setTrackedObject(GetScope()));
    EXPECT_CALL(*timer.callback, Call);
    EXPECT_CALL(dispatcher_, setTrackedObject(nullptr));
  } else {
    EXPECT_CALL(*timer.callback, Call)
        .WillOnce(
            [&] { timer.timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2)); })
        .WillOnce([] {});
  }

  timer.timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2), GetScope());
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  timer.pending_timer->invokeCallback();
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);

  EXPECT_TRUE(timer.timer->enabled());
  EXPECT_TRUE(timer.pending_timer->enabled());

  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  timer.pending_timer->invokeCallback();
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);

  EXPECT_FALSE(timer.timer->enabled());
  EXPECT_FALSE(timer.pending_timer->enabled());
}

TEST_P(ScaledRangeTimerManagerTestWithScope, ScheduleWithScalingFactorZero) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);

  RangeTimerGroup timer(manager, dispatcher_);
  manager.setScaleFactor(0);

  if (GetParam()) {
    InSequence s;
    EXPECT_CALL(dispatcher_, setTrackedObject(GetScope()));
    EXPECT_CALL(*timer.callback, Call);
    EXPECT_CALL(dispatcher_, setTrackedObject(nullptr));
  } else {
    EXPECT_CALL(*timer.callback, Call);
  }

  timer.timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(1), GetScope());
  dispatcher_.time_system_.timeSystem().advanceTimeWait(timer.pending_timer->duration_ms_);
  timer.pending_timer->invokeCallback();

  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
}

INSTANTIATE_TEST_SUITE_P(WithAndWithoutScope, ScaledRangeTimerManagerTestWithScope,
                         testing::Bool());

TEST_F(ScaledRangeTimerManagerTest, SingleTimerTriggeredNoScaling) {
  TestScaledRangeTimerManager manager(dispatcher_, 1.0);
  const MonotonicTime T0 = dispatcher_.timeSource().monotonicTime();

  RangeTimerGroup timer(manager, dispatcher_);
  EXPECT_CALL(
      *timer.pending_timer,
      enableTimer(InRange(std::chrono::seconds(4), std::chrono::seconds(7 /* > 4 + (9 - 4)/2 */)),
                  _));
  EXPECT_CALL(*timer.callback, Call());

  timer.timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(9));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(timer.pending_timer->duration_ms_);
  timer.pending_timer->invokeCallback();

  // Verify that there is at least one bucket whose timer is approximately 4 seconds.
  EXPECT_THAT(bucket_timers_, Contains(HasDeadline(T0 + std::chrono::seconds(9))));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(4));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
}

TEST_F(ScaledRangeTimerManagerTest, SingleTimerSameMinMax) {

  ScaledRangeTimerManager manager(dispatcher_, 1.0);
  RangeTimerGroup timer(manager, dispatcher_);
  EXPECT_CALL(*timer.callback, Call());

  EXPECT_CALL(*timer.pending_timer, enableTimer(std::chrono::milliseconds(1000), _));

  timer.timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(1));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  timer.pending_timer->invokeCallback();
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersNoScaling) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);
  std::vector<RangeTimerGroup> timers;

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, dispatcher_);
    EXPECT_CALL(*timers[i].callback, Call);
  }

  const MonotonicTime T0 = dispatcher_.timeSource().monotonicTime();
  timers[0].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(3));
  timers[1].timer->enableTimer(std::chrono::seconds(2), std::chrono::seconds(6));
  timers[2].timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(9));

  EXPECT_THAT(timers[0].pending_timer->enabled_, true);
  EXPECT_THAT(timers[1].pending_timer->enabled_, true);
  // timers[2] will be enabled even with a min of 0 because 9 seconds is not an exact bucket
  // duration.
  EXPECT_THAT(timers[2].pending_timer->enabled_, true);
  ASSERT_THAT(timers[0].pending_timer->duration_ms_, Le(timers[2].pending_timer->duration_ms_));

  // Advance time until timers[0] hits its min, then timers[2].
  dispatcher_.time_system_.timeSystem().advanceTimeWait(timers[0].pending_timer->duration_ms_);
  timers[0].pending_timer->invokeCallback();
  dispatcher_.time_system_.timeSystem().advanceTimeWait(timers[2].pending_timer->duration_ms_ -
                                                        timers[0].pending_timer->duration_ms_);
  timers[2].pending_timer->invokeCallback();
  EXPECT_THAT(timers[0].pending_timer->enabled_, false);
  EXPECT_THAT(timers[2].pending_timer->enabled_, false);
  EXPECT_THAT(bucket_timers_, Contains(HasDeadline(T0 + std::chrono::seconds(3))));
  EXPECT_THAT(bucket_timers_, Contains(HasDeadline(T0 + std::chrono::seconds(9))));

  dispatcher_.time_system_.timeSystem().advanceTimeWait(timers[1].pending_timer->duration_ms_ -
                                                        timers[2].pending_timer->duration_ms_);
  timers[1].pending_timer->invokeCallback();
  EXPECT_THAT(timers[1].pending_timer->enabled_, false);
  EXPECT_THAT(bucket_timers_, Contains(HasDeadline(T0 + std::chrono::seconds(6))));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(
      std::chrono::seconds(3) - (dispatcher_.timeSource().monotonicTime() - T0));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
  Mock::VerifyAndClearExpectations(timers[0].callback.get());

  // Now T = 3s; the minimum deadline is for timers[1] @ T = 6 seconds.
  ASSERT_EQ(dispatcher_.timeSource().monotonicTime(), T0 + std::chrono::seconds(3));
  EXPECT_THAT(bucket_timers_, Contains(HasDeadline(T0 + std::chrono::seconds(6))));

  // Advancing time in a big leap should be okay.
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(8));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersWithScaling) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);
  std::vector<RangeTimerGroup> timers;

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, dispatcher_);
    EXPECT_CALL(*timers[i].callback, Call);
  }

  // timers[0] will fire between T = 1 and T = 3.
  timers[0].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(3));
  // timers[1] will fire between T = 2 and T = 6.
  timers[1].timer->enableTimer(std::chrono::seconds(2), std::chrono::seconds(6));
  // timers[2] will fire between T = 6 and T = 10.
  timers[2].timer->enableTimer(std::chrono::seconds(6), std::chrono::seconds(10));

  manager.setScaleFactor(0.5);

  // Advance time to T = 1 second, so timers[0] hits its min.
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  timers[0].pending_timer->invokeCallback();
  EXPECT_THAT(timers[0].pending_timer->enabled_, false);

  // Advance time to T = 2, which should make timers[0] hit its scaled max.
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
  Mock::VerifyAndClearExpectations(timers[0].callback.get());
  timers[1].pending_timer->invokeCallback();

  // At 4x speed, timers[1] will fire in only 1 second.
  manager.setScaleFactor(0.25);

  // Advance time to T = 3, which should make timers[1] hit its scaled max.
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
  Mock::VerifyAndClearExpectations(timers[1].callback.get());

  // Advance time to T = 6, which enables timers[2] to fire.
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(3));
  timers[2].pending_timer->invokeCallback();
  manager.setScaleFactor(0);
  // With a scale factor of 0, timers[2] should be ready to be fired immediately.
  EXPECT_THAT(bucket_timers_, Contains(HasDeadline(dispatcher_.timeSource().monotonicTime())));
  fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_);
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersSameTimes) {
  ScaledRangeTimerManager manager(dispatcher_, 1.0);
  std::vector<RangeTimerGroup> timers;

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, dispatcher_);
    EXPECT_CALL(*timers[i].callback, Call);
    timers[i].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2));
  }

  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  for (int i = 0; i < 3; ++i) {
    timers[i].pending_timer->invokeCallback();
  }

  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  while (fireReadyTimers(dispatcher_.timeSource().monotonicTime(), bucket_timers_)) {
  }
}

} // namespace
} // namespace Event
} // namespace Envoy