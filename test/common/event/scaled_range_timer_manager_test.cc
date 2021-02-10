#include <chrono>

#include "envoy/event/timer.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/scaled_range_timer_manager.h"

#include "test/mocks/common.h"
#include "test/mocks/event/wrapped_dispatcher.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace {

using testing::ElementsAre;
using testing::InSequence;
using testing::MockFunction;

class ScopeTrackingDispatcher : public WrappedDispatcher {
public:
  ScopeTrackingDispatcher(DispatcherPtr dispatcher)
      : WrappedDispatcher(*dispatcher), dispatcher_(std::move(dispatcher)) {}

  const ScopeTrackedObject* setTrackedObject(const ScopeTrackedObject* object) override {
    scope_ = object;
    return impl_.setTrackedObject(object);
  }

  const ScopeTrackedObject* scope_{nullptr};

  Dispatcher* impl() const { return dispatcher_.get(); }

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

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);
  EXPECT_TRUE(timer->enabled());

  EXPECT_CALL(callback, Call());
  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);
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
  simTime().advanceTimeAndRun(std::chrono::seconds(10), dispatcher_, Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileDisabled) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  EXPECT_FALSE(timer->enabled());
  timer->disableTimer();

  EXPECT_FALSE(timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileWaitingForMin) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());
  timer->enableTimer(std::chrono::seconds(10), std::chrono::seconds(100));
  EXPECT_TRUE(timer->enabled());

  timer->disableTimer();
  EXPECT_FALSE(timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, DisableWhileScalingMax) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(100));

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);

  EXPECT_TRUE(timer->enabled());

  timer->disableTimer();
  EXPECT_FALSE(timer->enabled());

  // Run the dispatcher to make sure nothing happens when it's not supposed to.
  simTime().advanceTimeAndRun(std::chrono::seconds(100), dispatcher_, Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, InCallbackDisableLastTimerInSameQueue) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback1;
  auto timer1 = manager.createTimer(callback1.AsStdFunction());
  MockFunction<TimerCb> callback2;
  auto timer2 = manager.createTimer(callback2.AsStdFunction());

  timer1->enableTimer(std::chrono::seconds(0), std::chrono::seconds(95));
  timer2->enableTimer(std::chrono::seconds(5), std::chrono::seconds(100));

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);

  EXPECT_TRUE(timer1->enabled());
  EXPECT_TRUE(timer2->enabled());

  EXPECT_CALL(callback1, Call).WillOnce(Invoke([&]() {
    timer2->disableTimer();
    timer2.reset();
  }));

  // Run the dispatcher to make sure nothing happens when it's not supposed to.
  simTime().advanceTimeAndRun(std::chrono::seconds(100), dispatcher_, Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, InCallbackDisableTimerInOtherQueue) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback1;
  auto timer1 = manager.createTimer(callback1.AsStdFunction());
  MockFunction<TimerCb> callback2;
  auto timer2 = manager.createTimer(callback2.AsStdFunction());

  timer1->enableTimer(std::chrono::seconds(5), std::chrono::seconds(95));
  timer2->enableTimer(std::chrono::seconds(5), std::chrono::seconds(100));

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);

  EXPECT_TRUE(timer1->enabled());
  EXPECT_TRUE(timer2->enabled());

  EXPECT_CALL(callback1, Call).WillOnce(Invoke([&]() {
    timer2->disableTimer();
    timer2.reset();
  }));

  // Run the dispatcher to make sure nothing happens when it's not supposed to.
  simTime().advanceTimeAndRun(std::chrono::seconds(100), dispatcher_, Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, DisableWithZeroMinTime) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(100));

  EXPECT_TRUE(timer->enabled());

  timer->disableTimer();
  EXPECT_FALSE(timer->enabled());

  // Run the dispatcher to make sure nothing happens when it's not supposed to.
  simTime().advanceTimeAndRun(std::chrono::seconds(100), dispatcher_, Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, TriggerWithZeroMinTime) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());

  timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(10));

  simTime().advanceTimeAndRun(std::chrono::seconds(9), dispatcher_, Dispatcher::RunType::Block);
  EXPECT_CALL(callback, Call);
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, DisableFrontScalingMaxTimer) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback1, callback2;
  auto timer1 = manager.createTimer(callback1.AsStdFunction());
  auto timer2 = manager.createTimer(callback2.AsStdFunction());

  // These timers have the same max-min.
  timer1->enableTimer(std::chrono::seconds(5), std::chrono::seconds(30));
  timer2->enableTimer(std::chrono::seconds(10), std::chrono::seconds(35));

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);
  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);

  timer1->disableTimer();
  EXPECT_FALSE(timer1->enabled());
  ASSERT_TRUE(timer2->enabled());

  // Check that timer2 doesn't trigger when timer1 was originally going to, at start+30.
  simTime().advanceTimeAndRun(std::chrono::seconds(20), dispatcher_, Dispatcher::RunType::Block);

  // Advancing to timer2's max should trigger it.
  EXPECT_CALL(callback2, Call);
  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, DisableLaterScalingMaxTimer) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback1, callback2;
  auto timer1 = manager.createTimer(callback1.AsStdFunction());
  auto timer2 = manager.createTimer(callback2.AsStdFunction());

  // These timers have the same max-min.
  timer1->enableTimer(std::chrono::seconds(5), std::chrono::seconds(30));
  timer2->enableTimer(std::chrono::seconds(10), std::chrono::seconds(35));

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);
  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);

  timer2->disableTimer();
  EXPECT_FALSE(timer2->enabled());
  ASSERT_TRUE(timer1->enabled());

  // After the original windows for both timers have long expired, only the enabled one should fire.
  EXPECT_CALL(callback1, Call);
  simTime().advanceTimeAndRun(std::chrono::seconds(100), dispatcher_, Dispatcher::RunType::Block);
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
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  EXPECT_EQ(dispatcher_.scope_, nullptr);
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);
  EXPECT_EQ(dispatcher_.scope_, nullptr);

  EXPECT_TRUE(timer->enabled());

  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  EXPECT_FALSE(timer->enabled());
};

TEST_P(ScaledRangeTimerManagerTestWithScope, ScheduleWithScalingFactorZero) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());
  manager.setScaleFactor(0);

  EXPECT_CALL(callback, Call).WillOnce([&] { EXPECT_EQ(dispatcher_.scope_, getScope()); });

  timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(1), getScope());
  simTime().advanceTimeAndRun(std::chrono::milliseconds(1), dispatcher_,
                              Dispatcher::RunType::Block);
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

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);
  EXPECT_FALSE(triggered);

  simTime().advanceTimeAndRun(std::chrono::seconds(4) - std::chrono::milliseconds(1), dispatcher_,
                              Dispatcher::RunType::Block);
  EXPECT_FALSE(triggered);

  simTime().advanceTimeAndRun(std::chrono::milliseconds(1), dispatcher_,
                              Dispatcher::RunType::Block);
  EXPECT_TRUE(triggered);
}

TEST_F(ScaledRangeTimerManagerTest, SingleTimerSameMinMax) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback;
  auto timer = manager.createTimer(callback.AsStdFunction());
  EXPECT_CALL(callback, Call());

  timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(1));

  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);
  EXPECT_FALSE(timer->enabled());
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersNoScaling) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;
  timers.reserve(3);

  const MonotonicTime start = simTime().monotonicTime();
  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
  }

  timers[0].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(3));
  timers[1].timer->enableTimer(std::chrono::seconds(2), std::chrono::seconds(6));
  timers[2].timer->enableTimer(std::chrono::seconds(0), std::chrono::seconds(9));

  for (int i = 0; i < 10; ++i) {
    simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);
  }

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(start + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(start + std::chrono::seconds(6)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(start + std::chrono::seconds(9)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersWithScaling) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;
  timers.reserve(3);

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
  }

  const MonotonicTime start = simTime().monotonicTime();

  timers[0].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(3));
  timers[1].timer->enableTimer(std::chrono::seconds(2), std::chrono::seconds(6));
  timers[2].timer->enableTimer(std::chrono::seconds(6), std::chrono::seconds(10));

  manager.setScaleFactor(0.5);

  // Advance time to start = 1 second, so timers[0] hits its min.
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  // Advance time to start = 2, which should make timers[0] hit its scaled max.
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  // At 4x speed, timers[1] will fire in only 1 second.
  manager.setScaleFactor(0.25);

  // Advance time to start = 3, which should make timers[1] hit its scaled max.
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  // Advance time to start = 6, which is the minimum required for timers[2] to fire.
  simTime().advanceTimeAndRun(std::chrono::seconds(3), dispatcher_, Dispatcher::RunType::Block);

  manager.setScaleFactor(0);
  // With a scale factor of 0, timers[2] should be ready to be fired immediately.
  dispatcher_.run(Dispatcher::RunType::Block);

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(start + std::chrono::seconds(2)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(start + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(start + std::chrono::seconds(6)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersSameTimes) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;
  timers.reserve(3);

  const MonotonicTime start = simTime().monotonicTime();

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
    timers[i].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2));
  }

  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(start + std::chrono::seconds(2)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(start + std::chrono::seconds(2)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(start + std::chrono::seconds(2)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersSameTimesFastClock) {
  ScaledRangeTimerManager manager(dispatcher_);
  std::vector<TrackedTimer> timers;
  timers.reserve(3);

  const MonotonicTime start = simTime().monotonicTime();

  for (int i = 0; i < 3; ++i) {
    timers.emplace_back(manager, simTime());
    timers[i].timer->enableTimer(std::chrono::seconds(1), std::chrono::seconds(2));
  }

  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);
  // The clock runs fast here before the dispatcher gets to the timer callbacks.
  simTime().advanceTimeAndRun(std::chrono::seconds(2), dispatcher_, Dispatcher::RunType::Block);

  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(start + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(start + std::chrono::seconds(3)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(start + std::chrono::seconds(3)));
}

TEST_F(ScaledRangeTimerManagerTest, ScheduledWithScalingFactorZero) {
  ScaledRangeTimerManager manager(dispatcher_);
  manager.setScaleFactor(0);

  TrackedTimer timer(manager, simTime());

  // The timer should fire at start = 4 since the scaling factor is 0.
  const MonotonicTime start = simTime().monotonicTime();
  timer.timer->enableTimer(std::chrono::seconds(4), std::chrono::seconds(10));

  for (int i = 0; i < 10; ++i) {
    simTime().advanceTimeAndRun(std::chrono::seconds(4), dispatcher_, Dispatcher::RunType::Block);
  }

  EXPECT_THAT(*timer.trigger_times, ElementsAre(start + std::chrono::seconds(4)));
}

TEST_F(ScaledRangeTimerManagerTest, ScheduledWithMaxBeforeMin) {
  // When max < min, the timer behaves the same as if max == min. This ensures that min is always
  // respected, and max is respected as much as possible.
  ScaledRangeTimerManager manager(dispatcher_);

  TrackedTimer timer(manager, simTime());

  const MonotonicTime start = simTime().monotonicTime();
  timer.timer->enableTimer(std::chrono::seconds(4), std::chrono::seconds(3));

  for (int i = 0; i < 10; ++i) {
    simTime().advanceTimeAndRun(std::chrono::seconds(4), dispatcher_, Dispatcher::RunType::Block);
  }

  EXPECT_THAT(*timer.trigger_times, ElementsAre(start + std::chrono::seconds(4)));
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersTriggeredInTheSameEventLoopIteration) {
  ScaledRangeTimerManager manager(dispatcher_);

  MockFunction<TimerCb> callback1, callback2, callback3;
  auto timer1 = manager.createTimer(callback1.AsStdFunction());
  auto timer2 = manager.createTimer(callback2.AsStdFunction());
  auto timer3 = manager.createTimer(callback3.AsStdFunction());

  timer1->enableTimer(std::chrono::seconds(5), std::chrono::seconds(10));
  timer2->enableTimer(std::chrono::seconds(5), std::chrono::seconds(10));
  timer3->enableTimer(std::chrono::seconds(5), std::chrono::seconds(10));

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);

  DispatcherImpl* dispatcher_impl = static_cast<DispatcherImpl*>(dispatcher_.impl());
  ASSERT(dispatcher_impl != nullptr);

  ReadyWatcher prepare_watcher;
  evwatch_prepare_new(
      &dispatcher_impl->base(),
      +[](evwatch*, const evwatch_prepare_cb_info*, void* arg) {
        // `arg` contains the ReadyWatcher passed in from evwatch_prepare_new.
        auto watcher = static_cast<ReadyWatcher*>(arg);
        watcher->ready();
      },
      &prepare_watcher);

  ReadyWatcher schedulable_watcher;
  SchedulableCallbackPtr schedulable_callback =
      dispatcher_.createSchedulableCallback([&] { schedulable_watcher.ready(); });

  testing::Expectation first_prepare = EXPECT_CALL(prepare_watcher, ready());
  testing::ExpectationSet after_first_prepare;
  after_first_prepare +=
      EXPECT_CALL(schedulable_watcher, ready()).After(first_prepare).WillOnce([&] {
        schedulable_callback->scheduleCallbackNextIteration();
      });
  after_first_prepare += EXPECT_CALL(callback1, Call).After(first_prepare);
  after_first_prepare += EXPECT_CALL(callback2, Call).After(first_prepare);
  after_first_prepare += EXPECT_CALL(callback3, Call).After(first_prepare);
  testing::Expectation second_prepare =
      EXPECT_CALL(prepare_watcher, ready()).After(after_first_prepare).WillOnce([&] {
        schedulable_callback->scheduleCallbackNextIteration();
      });
  EXPECT_CALL(schedulable_watcher, ready()).After(second_prepare);

  // Running outside the event loop, this should schedule a run on the next event loop iteration.
  schedulable_callback->scheduleCallbackNextIteration();

  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);
  dispatcher_.run(Dispatcher::RunType::Block);
}

TEST_F(ScaledRangeTimerManagerTest, MultipleTimersWithChangeInScalingFactor) {
  ScaledRangeTimerManager manager(dispatcher_);
  const MonotonicTime start = simTime().monotonicTime();

  std::vector<TrackedTimer> timers;
  timers.reserve(4);
  for (int i = 0; i < 4; i++) {
    timers.emplace_back(manager, simTime());
  }

  timers[0].timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(15));
  timers[1].timer->enableTimer(std::chrono::seconds(12), std::chrono::seconds(14));

  manager.setScaleFactor(0.1);

  timers[2].timer->enableTimer(std::chrono::seconds(7), std::chrono::seconds(21));
  timers[3].timer->enableTimer(std::chrono::seconds(10), std::chrono::seconds(16));

  // Advance to timer 0's min.
  simTime().advanceTimeAndRun(std::chrono::seconds(5), dispatcher_, Dispatcher::RunType::Block);

  manager.setScaleFactor(0.5);

  // Now that the scale factor is 0.5, fire times are 0: start+10, 1: start+13, 2: start+14, 3:
  // start+13. Advance to timer 2's min.
  simTime().advanceTimeAndRun(std::chrono::seconds(2), dispatcher_, Dispatcher::RunType::Block);

  // Advance to time start+9.
  simTime().advanceTimeAndRun(std::chrono::seconds(2), dispatcher_, Dispatcher::RunType::Block);

  manager.setScaleFactor(0.1);
  // Now that the scale factor is reduced, fire times are 0: start+6, 1: start+12.2,
  // 2: start+8.4, 3: start+10.6. Timers 0 and 2 should fire immediately since their
  // trigger times are in the past.
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_THAT(*timers[0].trigger_times, ElementsAre(start + std::chrono::seconds(9)));
  EXPECT_THAT(*timers[2].trigger_times, ElementsAre(start + std::chrono::seconds(9)));

  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);

  // The time is now start+10. Re-enable timer 0.
  ASSERT_FALSE(timers[0].timer->enabled());
  timers[0].timer->enableTimer(std::chrono::seconds(5), std::chrono::seconds(13));

  // Fire times are now 0: start+19, 1: start+13, 2: none, 3: start+13.
  manager.setScaleFactor(0.5);

  // Advance to timer 1's min.
  simTime().advanceTimeAndRun(std::chrono::seconds(2), dispatcher_, Dispatcher::RunType::Block);

  // Advance again to start+13, which should trigger both timers 1 and 3.
  simTime().advanceTimeAndRun(std::chrono::seconds(1), dispatcher_, Dispatcher::RunType::Block);
  EXPECT_THAT(*timers[1].trigger_times, ElementsAre(start + std::chrono::seconds(13)));
  EXPECT_THAT(*timers[3].trigger_times, ElementsAre(start + std::chrono::seconds(13)));

  simTime().advanceTimeAndRun(std::chrono::seconds(3), dispatcher_, Dispatcher::RunType::Block);

  // The time is now start+16. Setting the scale factor to 0 should make timer 0 fire immediately.
  manager.setScaleFactor(0);
  dispatcher_.run(Dispatcher::RunType::Block);
  EXPECT_THAT(*timers[0].trigger_times,
              ElementsAre(start + std::chrono::seconds(9), start + std::chrono::seconds(16)));
}

} // namespace
} // namespace Event
} // namespace Envoy
