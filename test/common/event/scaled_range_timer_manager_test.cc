#include <chrono>

#include "envoy/event/timer.h"

#include "common/event/scaled_range_timer_manager.h"

#include "test/mocks/common.h"
#include "test/mocks/event/wrapped_dispatcher.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace {

using testing::_;
using testing::ElementsAre;
using testing::InSequence;
using testing::IsEmpty;
using testing::Mock;
using testing::MockFunction;
using testing::NiceMock;
using testing::StrictMock;

class ScopeTrackingDispatcher : public WrappedDispatcher {
public:
  ScopeTrackingDispatcher(DispatcherPtr dispatcher)
      : WrappedDispatcher(*dispatcher), dispatcher_(std::move(dispatcher)) {}

  const ScopeTrackedObject* setTrackedObject(const ScopeTrackedObject* object) override {
    scope_ = object;
    return impl_.setTrackedObject(object);
  }

  const ScopeTrackedObject* scope_{nullptr};

private:
  DispatcherPtr dispatcher_;
};

class ScaledRangeTimerManagerTest : public testing::Test, public TestUsingSimulatedTime {
public:
  ScaledRangeTimerManagerTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  ScopeTrackingDispatcher dispatcher_;
};

struct TrackedTimer {
  explicit TrackedTimer(ScaledRangeTimerManager& manager, TimeSystem& time_system)
      : timer(manager.createTimer([trigger_times = trigger_times.get(), &time_system] {
          trigger_times->push_back(time_system.monotonicTime());
        })) {}
  std::unique_ptr<std::vector<MonotonicTime>> trigger_times{
      std::make_unique<std::vector<MonotonicTime>>()};
  RangeTimerPtr timer;
};

TEST_F(ScaledRangeTimerManagerTest, CreateAndDestroy) {
  ScaledRangeTimerManager manager(dispatcher_);
}

TEST_F(ScaledRangeTimerManagerTest, CreateAndDestroyTimer) {
  ScaledRangeTimerManager manager(dispatcher_);

  {
    MockFunction<TimerCb> callback;
    auto timer = manager.createTimer(callback.AsStdFunction());
  }
}

TEST_F(ScaledRangeTimerManagerTest, CreateSingleScaledTimer) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(10));
  EXPECT_TRUE(timer->enabled());

  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_TRUE(timer->enabled());

  EXPECT_CALL(callback, Call());
  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_FALSE(timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, EnableAndDisableTimer) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(30));
  EXPECT_TRUE(timer->enabled());

  timer->disableTimer();
  EXPECT_FALSE(timer->enabled());

  // Provide some additional guarantee of safety by running the dispatcher for a little bit. This
  // should be a no-op, and if not (because a timer was fired), that's a problem that will be caught
  // by the strict mock callback.
  simTime().advanceTimeAsync(std::chrono::seconds(10));
  dispatcher_.run(Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileDisabled) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  EXPECT_FALSE(timer->enabled());
  timer->disableTimer();

  EXPECT_FALSE(timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileWaitingForMax) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());
  timer->enableTimer(std::chrono::seconds(10), std::chrono::seconds(100));
  EXPECT_TRUE(timer->enabled());

  timer->disableTimer();
  EXPECT_FALSE(timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileActive) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(100));

  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);

  EXPECT_TRUE(timer->enabled());

  timer->disableTimer();
  EXPECT_FALSE(timer->enabled());

  // Run the dispatcher to make sure nothing happens when it's not supposed to.
  simTime().advanceTimeAsync(std::chrono::seconds(100));
  dispatcher_.run(Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, DisableFrontActiveTimer) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback1, callback2;
  auto timer1 = manager.createTimer(callback1.AsStdFunction());
  auto timer2 = manager.createTimer(callback2.AsStdFunction());

  // These timers have the same max-min.
  timer1->enableTimer(std::chrono::seconds(5), std::chrono::seconds(30));
  timer2->enableTimer(std::chrono::seconds(10), std::chrono::seconds(35));

  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);
  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);

  timer1->disableTimer();
  EXPECT_FALSE(timer1->enabled());
  ASSERT_TRUE(timer2->enabled());

  // After the original windows for both timers have long expired, only the enabled one should fire.
  EXPECT_CALL(callback2, Call);
  simTime().advanceTimeAsync(std::chrono::seconds(100));
  dispatcher_.run(Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, DisableLaterActiveTimer) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback1, callback2;
  auto timer1 = manager.createTimer(callback1.AsStdFunction());
  auto timer2 = manager.createTimer(callback2.AsStdFunction());

  // These timers have the same max-min.
  timer1->enableTimer(std::chrono::seconds(5), std::chrono::seconds(30));
  timer2->enableTimer(std::chrono::seconds(10), std::chrono::seconds(35));

  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);
  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);

  timer2->disableTimer();
  EXPECT_FALSE(timer2->enabled());
  ASSERT_TRUE(timer1->enabled());

  // After the original windows for both timers have long expired, only the enabled one should fire.
  EXPECT_CALL(callback1, Call);
  simTime().advanceTimeAsync(std::chrono::seconds(100));
  dispatcher_.run(Dispatcher::RunType::Block);
}

class ScaledRangeTimerManagerTestWithScope : public ScaledRangeTimerManagerTest,
                                             public testing::WithParamInterface<bool> {
public:
  ScopeTrackedObject* getScope() { return GetParam() ? &scope_ : nullptr; }
  MockScopedTrackedObject scope_;
};

TEST_P(ScaledRangeTimerManagerTestWithScope, ReRegisterOnCallback) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  EXPECT_EQ(dispatcher_.scope_, nullptr);
  {
    InSequence s;
    EXPECT_CALL(callback, Call).WillOnce([&] {
      EXPECT_EQ(dispatcher_.scope_, getScope());
      timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2), getScope());
    });
    EXPECT_CALL(callback, Call).WillOnce([&] { EXPECT_EQ(dispatcher_.scope_, getScope()); });
  }

  timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2), getScope());
  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  simTime().advanceTimeAsync(std::chrono::seconds(1));
  EXPECT_EQ(dispatcher_.scope_, nullptr);
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_EQ(dispatcher_.scope_, nullptr);

  EXPECT_TRUE(timer->enabled());

  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);
  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  EXPECT_FALSE(timer->enabled());
};

TEST_P(ScaledRangeTimerManagerTestWithScope, ScheduleWithScalingFactorZero) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());
  manager.setScaleFactor(0);

  EXPECT_CALL(callback, Call).WillOnce([&] { EXPECT_EQ(dispatcher_.scope_, getScope()); });

  timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(1), getScope());
  simTime().advanceTimeAsync(std::chrono::milliseconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);
}

INSTANTIATE_TEST_SUITE_P(WithAndWithoutScope, ScaledRangeTimerManagerTestWithScope,
                         testing::Bool());

TEST_F(ScaledRangeTimerManagerTest, SingleTimerTriggeredNoScaling) {
  ScaledRangeTimerManager manager(dispatcher_);
  bool triggered = false;

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());
  EXPECT_CALL(callback, Call()).WillOnce([&] { triggered = true; });

  timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(9));

  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_FALSE(triggered);

  simTime().advanceTimeAsync(std::chrono::seconds(4) - std::chrono::milliseconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_FALSE(triggered);

  simTime().advanceTimeAsync(std::chrono::milliseconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_TRUE(triggered);
}

TEST_F(ScaledRangeTimerManagerTest, SingleTimerSameMinMax) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());
  EXPECT_CALL(callback, Call());

  timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(1));

  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersNoScaling) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;

  const MonotonicTime T = simTime().monotonicTime();
  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
  }

  timers[0].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(3));
  timers[1].timer->enableTimer(std::chrono::seconds(2), std::chrono::seconds(6));
  timers[2].timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(9));

  for (int i = 0; i < 10; ++i) {
    simTime().advanceTimeAsync(std::chrono::seconds(1));
    dispatcher_.run(Dispatcher::RunType::Block);
  }

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(T + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(T + std::chrono::seconds(6)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(T + std::chrono::seconds(9)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersWithScaling) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
  }

  const MonotonicTime T = simTime().monotonicTime();

  timers[0].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(3));
  timers[1].timer->enableTimer(std::chrono::seconds(2), std::chrono::seconds(6));
  timers[2].timer->enableTimer(std::chrono::seconds(6), std::chrono::seconds(10));

  manager.setScaleFactor(0.5);

  // Advance time to T = 1 second, so timers[0] hits its min.
  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  // Advance time to T = 2, which should make timers[0] hit its scaled max.
  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  // At 4x speed, timers[1] will fire in only 1 second.
  manager.setScaleFactor(0.25);

  // Advance time to T = 3, which should make timers[1] hit its scaled max.
  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  // Advance time to T = 6, which is the minimum required for timers[2] to fire.
  simTime().advanceTimeAsync(std::chrono::seconds(3));
  dispatcher_.run(Dispatcher::RunType::Block);

  manager.setScaleFactor(0);
  // With a scale factor of 0, timers[2] should be ready to be fired immediately.
  dispatcher_.run(Dispatcher::RunType::Block);

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(T + std::chrono::seconds(2)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(T + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(T + std::chrono::seconds(6)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersSameTimes) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;

  const MonotonicTime T = simTime().monotonicTime();

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
    timers[i].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2));
  }

  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(T + std::chrono::seconds(2)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(T + std::chrono::seconds(2)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(T + std::chrono::seconds(2)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersSameTimesFastClock) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;

  const MonotonicTime T = simTime().monotonicTime();

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
    timers[i].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2));
  }

  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);
  // The clock runs fast here before the dispatcher gets to the timer callbacks.
  simTime().advanceTimeAsync(std::chrono::seconds(2));
  dispatcher_.run(Dispatcher::RunType::Block);

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(T + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(T + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(T + std::chrono::seconds(3)));
}

TEST_F(ScaledRangeTimerManagerTest, ScheduledWithScalingFactorZero) {
  ScaledRangeTimerManager manager(dispatcher_);
  manager.setScaleFactor(0);

  TrackedTimer timer(manager, simTime());

  // The timer should fire at T = 4 since the scaling factor is 0.
  const MonotonicTime T = simTime().monotonicTime();
  timer.timer->enableTimer(std::chrono::seconds(4), std::chrono::seconds(10));

  for (int i = 0; i < 10; ++i) {
    simTime().advanceTimeAsync(std::chrono::seconds(4));
    dispatcher_.run(Dispatcher::RunType::Block);
  }

  EXPECT_THAT(*timer.trigger_times, ElementsAre(T + std::chrono::seconds(4)));
}

TEST_F(ScaledRangeTimerManagerTest, ScheduledWithMaxBeforeMin) {
  ScaledRangeTimerManager manager(dispatcher_);

  TrackedTimer timer(manager, simTime());

  const MonotonicTime T = simTime().monotonicTime();
  timer.timer->enableTimer(std::chrono::seconds(4), std::chrono::seconds(3));

  for (int i = 0; i < 10; ++i) {
    simTime().advanceTimeAsync(std::chrono::seconds(4));
    dispatcher_.run(Dispatcher::RunType::Block);
  }

  EXPECT_THAT(*timer.trigger_times, ElementsAre(T + std::chrono::seconds(4)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersWithChangeInScalingFactor) {
  ScaledRangeTimerManager manager(dispatcher_);
  const MonotonicTime T = simTime().monotonicTime();

  std::vector<TrackedTimer> timers;
  for (int i = 0; i < 4; i++) {
    timers.emplace_back(manager, simTime());
  }

  timers[0].timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(15));
  timers[1].timer->enableTimer(std::chrono::seconds(12), std::chrono::seconds(14));

  manager.setScaleFactor(0.1);

  timers[2].timer->enableTimer(std::chrono::seconds(7), std::chrono::seconds(21));
  timers[3].timer->enableTimer(std::chrono::seconds(10), std::chrono::seconds(16));

  // Advance to timer 0's min.
  simTime().advanceTimeAsync(std::chrono::seconds(5));
  dispatcher_.run(Dispatcher::RunType::Block);

  manager.setScaleFactor(0.5);

  // Now that the scale factor is 0.5, fire times are 0: T+10, 1: T+13, 2: T+14, 3: T+13.
  // Advance to timer 2's min.
  simTime().advanceTimeAsync(std::chrono::seconds(2));
  dispatcher_.run(Dispatcher::RunType::Block);

  // Advance to time T+9.
  simTime().advanceTimeAsync(std::chrono::seconds(2));
  dispatcher_.run(Dispatcher::RunType::Block);

  manager.setScaleFactor(0.1);
  // Now that the scale factor is reduced, fire times are 0: T+6, 1: T+12.2,
  // 2: T+8.4, 3: T+10.6. Timers 0 and 2 should fire immediately since their
  // trigger times are in the past.
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(T + std::chrono::seconds(9)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(T + std::chrono::seconds(9)));

  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);

  // The time is now T+10. Re-enable timer 0.
  ASSERT_FALSE(timers[0].timer->enabled());
  timers[0].timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(13));

  // Fire times are now 0: T+19, 1: T+13, 2: none, 3: T+13.
  manager.setScaleFactor(0.5);

  // Advance to timer 1's min.
  simTime().advanceTimeAsync(std::chrono::seconds(2));
  dispatcher_.run(Dispatcher::RunType::Block);

  // Advance again to T+13, which should trigger both timers 1 and 3.
  simTime().advanceTimeAsync(std::chrono::seconds(1));
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(T + std::chrono::seconds(13)));
  EXPECT_THAT(*timers[3].trigger_times, ElementsAre(T + std::chrono::seconds(13)));

  simTime().advanceTimeAsync(std::chrono::seconds(3));
  dispatcher_.run(Dispatcher::RunType::Block);

  // The time is now T+16. Setting the scale factor to 0 should make timer 0 fire immediately.
  manager.setScaleFactor(0);
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_THAT(*timers[0].trigger_times,
              ElementsAre(T + std::chrono::seconds(9), T + std::chrono::seconds(16)));
}

} // namespace
} // namespace Event
} // namespace Envoy
