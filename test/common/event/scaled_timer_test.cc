#include "common/event/scaled_timer.h"

#include "envoy/event/timer.h"
#include "test/test_common/simulated_time_system.h"
#include "test/mocks/event/mocks.h"

#include "gtest/gtest.h"
#include <chrono>

namespace Envoy {
namespace Event {
namespace {

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::DoAll;
using testing::InSequence;
using testing::InvokeArgument;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

class ScaledTimerManagerTest : public testing::Test {
public:
  ScaledTimerManagerTest() {
    ON_CALL(dispatcher_, createTimer_)
        .WillByDefault(DoAll(SaveArg<0>(&timer_cb_), ReturnNew<NiceMock<MockTimer>>()));
  }

  SimulatedTimeSystem simulated_time;
  MockDispatcher dispatcher_;
  TimerCb timer_cb_;
};

TEST_F(ScaledTimerManagerTest, CreatesDispatcherTimer) {
  EXPECT_CALL(dispatcher_, createTimer_).WillOnce(ReturnNew<NiceMock<MockTimer>>());

  ScaledTimerManager manager(dispatcher_);
}

TEST_F(ScaledTimerManagerTest, CreateSingleScaledTimer) {
  MockFunction<void()> cb;
  auto timer = std::make_unique<MockTimer>();

  EXPECT_CALL(*timer, enableHRTimer(std::chrono::microseconds(5 * 1000 * 1000), _));
  EXPECT_CALL(*timer, disableTimer);
  EXPECT_CALL(dispatcher_, createTimer_)
      .WillOnce(DoAll(SaveArg<0>(&timer_cb_), Return(ByMove(timer.release()))));

  EXPECT_CALL(cb, Call());

  ScaledTimerManager manager(dispatcher_);

  auto scaled_timer = manager.createTimer(cb.AsStdFunction());
  scaled_timer->enableTimer(std::chrono::seconds(5));

  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(5));
  timer_cb_();
}

TEST_F(ScaledTimerManagerTest, CreateAndDeleteTimer) {
  MockFunction<void()> cb;
  auto timer = std::make_unique<MockTimer>();

  EXPECT_CALL(dispatcher_, createTimer_)
      .WillOnce(DoAll(SaveArg<0>(&timer_cb_), Return(ByMove(timer.release()))));

  ScaledTimerManager manager(dispatcher_);

  {
    auto scaled_timer = manager.createTimer(cb.AsStdFunction());
    scaled_timer.reset();
  }
}

TEST_F(ScaledTimerManagerTest, EnableAndDisableTimer) {
  MockFunction<void()> cb;
  auto timer = std::make_unique<MockTimer>();

  EXPECT_CALL(*timer, enableHRTimer(std::chrono::microseconds(5 * 1000 * 1000), _));
  EXPECT_CALL(*timer, disableTimer).Times(2);
  EXPECT_CALL(dispatcher_, createTimer_)
      .WillOnce(DoAll(SaveArg<0>(&timer_cb_), Return(ByMove(timer.release()))));

  ScaledTimerManager manager(dispatcher_);

  auto scaled_timer = manager.createTimer(cb.AsStdFunction());
  scaled_timer->enableTimer(std::chrono::seconds(5));
  scaled_timer->disableTimer();

  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(5));
  timer_cb_();
}

TEST_F(ScaledTimerManagerTest, EnableMultipleTimers) {
  auto timer = std::make_unique<MockTimer>();

  EXPECT_CALL(*timer, enableHRTimer(std::chrono::microseconds(1 * 1000 * 1000), _))
      .Times(
          // Expect one call as each timer is enabled, then another one after the first timer fires,
          // then the last one after the second timer fires.
          3 + 1 + 1);
  EXPECT_CALL(*timer, disableTimer);
  EXPECT_CALL(dispatcher_, createTimer_)
      .WillOnce(DoAll(SaveArg<0>(&timer_cb_), Return(ByMove(timer.release()))));

  MockFunction<void(int)> cb;
  {
    InSequence s;
    EXPECT_CALL(cb, Call(1));
    EXPECT_CALL(cb, Call(2));
    EXPECT_CALL(cb, Call(3));
  }

  ScaledTimerManager manager(dispatcher_);

  std::vector<TimerPtr> timers;
  for (int i = 1; i <= 3; i++) {
    timers.push_back(manager.createTimer([i, &cb] { cb.Call(i); }));
    timers.back()->enableTimer(std::chrono::seconds(i));
  }

  for (int i = 0; i < 3; i++) {
    dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
    timer_cb_();
  }
}

// Test that if timers are scheduled for some times in the future, and then the scaling factor
// changes, the timers will trigger earlier than scheduled.
TEST_F(ScaledTimerManagerTest, ScaleMultipleTimers) {
  auto timer = std::make_unique<MockTimer>();

  // Expect 3 calls with the original 10-second timeout, then, when the scaling factor is changed, a
  // single 1-second timeout for each managed timer.
  EXPECT_CALL(*timer, enableHRTimer(std::chrono::microseconds(10 * 1000 * 1000), _)).Times(3);
  EXPECT_CALL(*timer, enableHRTimer(std::chrono::microseconds(1 * 1000 * 1000), _)).Times(3);
  EXPECT_CALL(*timer, disableTimer);
  EXPECT_CALL(dispatcher_, createTimer_)
      .WillOnce(DoAll(SaveArg<0>(&timer_cb_), Return(ByMove(timer.release()))));

  MockFunction<void(int)> cb;
  {
    InSequence s;
    EXPECT_CALL(cb, Call(1));
    EXPECT_CALL(cb, Call(2));
    EXPECT_CALL(cb, Call(3));
  }

  ScaledTimerManager manager(dispatcher_);

  std::vector<TimerPtr> timers;
  for (int i = 1; i <= 3; i++) {
    timers.push_back(manager.createTimer([i, &cb] { cb.Call(i); }));
    timers.back()->enableTimer(std::chrono::seconds(10 * i));
  }

  // Each duration will be treated as if it was 10% of the original requested value.
  manager.setDurationScaleFactor(0.1);

  for (int i = 0; i < 3; i++) {
    dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
    timer_cb_();
  }
}

// If two timers are scheduled at different times with different timeouts, they can be reordered by
// a change in the scale factor.
TEST_F(ScaledTimerManagerTest, TimersReorderedByScaling) {
  auto timer = std::make_unique<MockTimer>();
  std::chrono::microseconds timer_delay;

  EXPECT_CALL(*timer, enableHRTimer).Times(AnyNumber()).WillRepeatedly(SaveArg<0>(&timer_delay));
  EXPECT_CALL(*timer, disableTimer);
  EXPECT_CALL(dispatcher_, createTimer_)
      .WillOnce(DoAll(SaveArg<0>(&timer_cb_), Return(ByMove(timer.release()))));

  MockFunction<void(char)> cb;
  {
    InSequence s;
    EXPECT_CALL(cb, Call('A'));
    EXPECT_CALL(cb, Call('B'));
  }

  ScaledTimerManager manager(dispatcher_);

  auto first_timer = manager.createTimer([&cb] { cb.Call('A'); });
  auto second_timer = manager.createTimer([&cb] { cb.Call('B'); });

  // Let t0 be the starting time. Set timer A at t0 + 20s
  first_timer->enableTimer(std::chrono::seconds(20));

  // Advance the clock to t1 = t0 + 10s.
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(10));

  // Set timer B at t1 + 5s = t0 + 15s, so that it would fire before timer A.
  second_timer->enableTimer(std::chrono::seconds(5));

  // Adjust the scale factor so that timer A will fire at t0 + (.6 * 20s) = t1 + 2s, and timer B will
  // fire at t1 + (.6 * 5s) = t1 + 3s.
  manager.setDurationScaleFactor(0.6);

  // At this point, the timer should be set for 2 seconds. Advancing by that much will trigger timer
  // A but not timer B.
  EXPECT_EQ(timer_delay, std::chrono::seconds(2));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(2));
  timer_cb_();

  EXPECT_FALSE(first_timer->enabled());
  EXPECT_TRUE(second_timer->enabled());

  // Advance the timer again by another second, which should trigger timer B.
  EXPECT_EQ(timer_delay, std::chrono::seconds(1));
  dispatcher_.time_system_.timeSystem().advanceTimeWait(std::chrono::seconds(1));
  timer_cb_();

  EXPECT_FALSE(second_timer->enabled());
}

} // namespace
} // namespace Event
} // namespace Envoy