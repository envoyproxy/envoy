#include <chrono>
#include <memory>
#include <optional>
#include <type_traits>

#include "source/extensions/resource_monitors/adaptive_worker_capacity/worker_utilization_tracker.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {
namespace {

static_assert(!std::is_copy_constructible_v<WorkerUtilizationTracker>);
static_assert(!std::is_copy_assignable_v<WorkerUtilizationTracker>);
static_assert(!std::is_move_constructible_v<WorkerUtilizationTracker>);
static_assert(!std::is_move_assignable_v<WorkerUtilizationTracker>);

using testing::NiceMock;

TEST(WorkerUtilizationTrackerTest, CalculatesUtilizationCorrectly) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Time T0: tracker starts.
  // Dispatcher wakes up at T0 + 10ms (onCheck).
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  const auto check1 = time_system.monotonicTime();
  tracker.onCheck(check1);

  // Active event processing for 20ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(20));
  const auto prepare1 = time_system.monotonicTime();
  tracker.onPrepare(prepare1, std::nullopt);

  // Epoll wait for 70ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(70));

  // Polling at T0 + 100ms: busy = 20ms, wall = 100ms -> utilization = 0.20
  const double util = tracker.sampleUtilization();
  EXPECT_DOUBLE_EQ(util, 0.20);
}

TEST(WorkerUtilizationTrackerTest, HandlesInCheckDuringPoll) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Dispatcher wakes up at T0 + 10ms (onCheck).
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  tracker.onCheck(time_system.monotonicTime());

  // Active event processing for 40ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(40));

  // Polling occurs at T0 + 50ms while still in check (in active callback).
  // busy = 40ms, wall = 50ms -> utilization = 0.80
  const double util = tracker.sampleUtilization();
  EXPECT_DOUBLE_EQ(util, 0.80);
}

TEST(WorkerUtilizationTrackerTest, IncludesWakeupDelayInUtilization) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Tracker starts at T0.
  // onPrepare sets timeout of 10ms at T0. Expected wakeup = T0 + 10ms.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(10));

  // System experiences CPU starvation/delay: epoll wakes up at T0 + 30ms (20ms
  // late!).
  time_system.advanceTimeWait(std::chrono::milliseconds(30));
  tracker.onCheck(time_system.monotonicTime());

  // Active event processing for 10ms until T0 + 40ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);

  // Polling at T0 + 50ms: busy = 20ms (wakeup delay) + 10ms (active work) =
  // 30ms. wall = 50ms -> utilization = 30 / 50 = 0.60
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  const double util = tracker.sampleUtilization();
  EXPECT_DOUBLE_EQ(util, 0.60);
}

TEST(WorkerUtilizationTrackerTest, HandlesCallbackSpanningMultiplePollPeriods) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // T0 + 50ms: onCheck starts a long 300ms callback.
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  tracker.onCheck(time_system.monotonicTime());

  // Poll 1 at T0 + 100ms: busy = 50ms, wall = 100ms -> utilization = 0.50
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.50);

  // Poll 2 at T0 + 200ms: busy = 100ms, wall = 100ms -> utilization = 1.00
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.00);

  // Poll 3 at T0 + 300ms: busy = 100ms, wall = 100ms -> utilization = 1.00
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.00);

  // T0 + 350ms: long callback finishes (onPrepare).
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);

  // Poll 4 at T0 + 400ms: busy = 50ms (300-350ms active), wall = 100ms ->
  // utilization = 0.50
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.50);

  // Poll 5 at T0 + 500ms: idle, wall = 100ms -> utilization = 0.00
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.00);
}

TEST(WorkerUtilizationTrackerTest, ReturnsZeroUtilizationWhenDeltaWallIsZero) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);
  const auto t0 = time_system.monotonicTime();

  // Zero elapsed time since construction returns 0.0.
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.0);

  // Accumulate 50ms of busy time while dispatcher_.timeSource().monotonicTime()
  // remains at t0 (delta_wall == 0); sampleUtilization() returns 0.0 without
  // discarding accumulated_busy_time_.
  tracker.onCheck(t0);
  tracker.onPrepare(t0 + std::chrono::milliseconds(50), std::nullopt);
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.0);

  // Advancing wall time by 100ms reports the preserved 50ms of busy time.
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.50);
}

TEST(WorkerUtilizationTrackerTest, RegistersOnConstructionAndUnregistersOnDestruction) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  Event::Evwatch::Observer* registered_observer = nullptr;
  EXPECT_CALL(dispatcher, registerEvwatchObserver(testing::_))
      .WillOnce(testing::Invoke(
          [&](Event::Evwatch::Observer& observer) { registered_observer = &observer; }));
  EXPECT_CALL(dispatcher, unregisterEvwatchObserver(testing::_)).Times(0);

  auto tracker = std::make_unique<WorkerUtilizationTracker>(dispatcher);
  EXPECT_EQ(registered_observer, static_cast<Event::Evwatch::Observer*>(tracker.get()));
  testing::Mock::VerifyAndClearExpectations(&dispatcher);

  EXPECT_CALL(dispatcher, unregisterEvwatchObserver(testing::Ref(*tracker)));
  tracker.reset();
}

// When busy time exceeds wall time within a polling window, cap
// utilization at 1.0 (100%) and clamp without carrying over excess busy time.
TEST(WorkerUtilizationTrackerTest, ClampsUtilizationToOneWithoutCarryOver) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // onPrepare sets a 10ms timeout at T0. Expected wakeup = T0 + 10ms.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(10));

  // Severe stall: wakeup happens at T0 + 150ms (140ms late!), which exceeds
  // the wall time window of 100ms between polls.
  tracker.onCheck(time_system.monotonicTime() + std::chrono::milliseconds(150));

  // Sample utilization at T0 + 100ms wall time: delta_busy (140ms) > delta_wall
  // (100ms) clamps ratio at 1.0 and does NOT carry over excess.
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.0);

  // In the next 100ms polling window, the in-flight busy time (from 150ms to
  // 200ms = 50ms) is reported without any carry-over from the previous window
  // (50ms total / 100ms = 0.50).
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.50);
}

TEST(WorkerUtilizationTrackerTest, HandlesEarlyWakeupWithoutNegativeUtilization) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Tracker starts at T0.
  // onPrepare sets timeout of 50ms at T0. Expected wakeup = T0 + 50ms.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(50));

  // Early wakeup: epoll wakes up at T0 + 20ms (30ms early!).
  time_system.advanceTimeWait(std::chrono::milliseconds(20));
  tracker.onCheck(time_system.monotonicTime());

  // Active event processing for 10ms until T0 + 30ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);

  // Polling at T0 + 100ms: busy = 0ms (negative delay clamped to 0) + 10ms
  // (active work) = 10ms. wall = 100ms -> utilization = 10 / 100 = 0.10
  time_system.advanceTimeWait(std::chrono::milliseconds(70));
  const double util = tracker.sampleUtilization();
  EXPECT_DOUBLE_EQ(util, 0.10);
}

TEST(WorkerUtilizationTrackerTest, HandlesZeroTimeoutNonBlockingPoll) {
  Event::SimulatedTimeSystem time_system;
  time_system.advanceTimeWait(std::chrono::milliseconds(10));

  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // onPrepare with 0 timeout (e.g. non-blocking epoll or immediate min-heap
  // timer). Expected wakeup = T0.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(0));

  // Epoll syscall takes 5ms to return. Since expected wakeup was T0, all 5ms
  // is attributed to wakeup delay (busy time).
  time_system.advanceTimeWait(std::chrono::milliseconds(5));
  tracker.onCheck(time_system.monotonicTime());

  // Active event processing for 15ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(15));
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);

  // Polling at T0 + 100ms: busy = 5ms (syscall latency) + 15ms (callback work)
  // = 20ms. wall = 100ms -> utilization = 0.20
  time_system.advanceTimeWait(std::chrono::milliseconds(80));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.20);
}

TEST(WorkerUtilizationTrackerTest, AccumulatesBusyTimeAcrossMultipleIterations) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Iteration 1: indefinite sleep (std::nullopt) for 15ms (0ms busy), then 15ms active work,
  // followed by a 10ms poll timeout (expected wakeup at T0 + 40ms).
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(15));
  tracker.onCheck(time_system.monotonicTime());
  time_system.advanceTimeWait(std::chrono::milliseconds(15));
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(10));

  // Iteration 2: wakes at T0 + 60ms (20ms wakeup delay) and runs 40ms of active work when
  // sampled mid-callback at T0 + 100ms -> (15 + 20 + 40) / 100 = 0.75.
  time_system.advanceTimeWait(std::chrono::milliseconds(30));
  tracker.onCheck(time_system.monotonicTime());
  time_system.advanceTimeWait(std::chrono::milliseconds(40));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.75);
}

} // namespace
} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
