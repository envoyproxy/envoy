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

  // Initial active callback work for 5ms before the first onPrepare.
  time_system.advanceTimeWait(std::chrono::milliseconds(5));
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);

  // Epoll wait for 75ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(75));
  tracker.onCheck(time_system.monotonicTime());

  // Active event processing for 20ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(20));

  // Polling at T0 + 100ms: idle = 75ms, wall = 100ms -> utilization = 1.0 - 75 / 100 = 0.25.
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.25);
}

TEST(WorkerUtilizationTrackerTest, IncludesWakeupDelayInUtilization) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // onPrepare sets timeout of 10ms at T0.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(10));

  // System experiences CPU starvation/delay: epoll wakes up at T0 + 30ms (only 10ms idle; the 20ms
  // wakeup delay counts as busy).
  time_system.advanceTimeWait(std::chrono::milliseconds(30));
  tracker.onCheck(time_system.monotonicTime());

  // Active event processing for 20ms until T0 + 50ms: idle = 10ms, wall = 50ms -> 0.80.
  time_system.advanceTimeWait(std::chrono::milliseconds(20));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.80);
}

TEST(WorkerUtilizationTrackerTest, HandlesContinuousActiveExecutionAcrossMultipleSamples) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Sleep 50ms, then wake up at T0 + 50ms and execute active callbacks through T0 + 350ms.
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  tracker.onCheck(time_system.monotonicTime());

  // Poll 1 at T0 + 100ms: idle = 50ms, wall = 100ms -> utilization = 0.50.
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.50);

  // Poll 2 at T0 + 200ms: idle = 0ms, wall = 100ms -> utilization = 1.00.
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.00);

  // Poll 3 at T0 + 300ms: idle = 0ms, wall = 100ms -> utilization = 1.00.
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.00);

  // T0 + 350ms: active callbacks finish and dispatcher sleeps 50ms until T0 + 400ms.
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  tracker.onCheck(time_system.monotonicTime());

  // Poll 4 at T0 + 400ms: idle = 50ms, wall = 100ms -> utilization = 0.50.
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.50);

  // Poll 5 at T0 + 500ms: pure idle 400ms -> 500ms, wall = 100ms -> utilization = 0.00.
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  tracker.onCheck(time_system.monotonicTime());
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.00);
}

TEST(WorkerUtilizationTrackerTest, ReturnsZeroUtilizationWhenDeltaWallIsZero) {
  Event::SimulatedTimeSystem time_system;
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);
  const auto t0 = time_system.monotonicTime();

  // Zero elapsed time since construction returns 0.0.
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.0);

  // Accumulate 50ms of idle time while dispatcher_.timeSource().monotonicTime() remains at t0
  // (delta_wall == 0); sampleUtilization() returns 0.0 without discarding accumulated_idle_time_.
  tracker.onPrepare(t0 - std::chrono::milliseconds(50), std::nullopt);
  tracker.onCheck(t0);
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.0);

  // Advancing wall time by 100ms reports the preserved 50ms of idle time (1.0 - 50 / 100 = 0.50).
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

TEST(WorkerUtilizationTrackerTest, ClampsExcessIdleTimeToZeroWithoutCarryOver) {
  Event::SimulatedTimeSystem time_system;
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Sleep interval starts 50ms before last_sample_time_ (T0) and wakes at T0 + 100ms, yielding
  // 150ms of idle time across a 100ms wall-time window.
  tracker.onPrepare(time_system.monotonicTime() - std::chrono::milliseconds(50), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  tracker.onCheck(time_system.monotonicTime());

  // 1.0 - (150ms / 100ms) = -0.50 clamps to 0.0, and the excess 50ms is discarded.
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.0);

  // In the next 100ms active window, 0ms idle is reported (no carry-over from previous window).
  time_system.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.0);
}

TEST(WorkerUtilizationTrackerTest, HandlesEarlyWakeupWithoutOvercountingIdleTime) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // onPrepare sets timeout of 50ms at T0.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(50));

  // Early wakeup: epoll wakes up at T0 + 20ms (20ms idle, not the 50ms timeout).
  time_system.advanceTimeWait(std::chrono::milliseconds(20));
  tracker.onCheck(time_system.monotonicTime());

  // Active event processing for 80ms until T0 + 100ms -> 1.0 - 20 / 100 = 0.80.
  time_system.advanceTimeWait(std::chrono::milliseconds(80));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.80);
}

TEST(WorkerUtilizationTrackerTest, HandlesZeroTimeoutAndZeroElapsedPolls) {
  Event::SimulatedTimeSystem time_system;
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Unmatched initial onCheck before any onPrepare records 0ms idle -> 1.0 utilization.
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  tracker.onCheck(time_system.monotonicTime());
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.0);

  // onPrepare with 0 timeout: 5ms syscall counts as busy (0ms idle), followed by 15ms callback
  // and 80ms indefinite sleep -> 1.0 - 80 / 100 = 0.20.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(0));
  time_system.advanceTimeWait(std::chrono::milliseconds(5));
  tracker.onCheck(time_system.monotonicTime());
  time_system.advanceTimeWait(std::chrono::milliseconds(15));
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(80));
  tracker.onCheck(time_system.monotonicTime());
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.20);

  // Instant 0ms-elapsed poll (check_time == prepare_time) still clears pending_poll_: debug
  // builds check ASSERT(!pending_poll_.has_value()) in the first sampleUtilization(), and release
  // builds verify a subsequent unmatched onCheck does not consume a stale prepare_time.
  const auto t_instant = time_system.monotonicTime();
  tracker.onPrepare(t_instant, std::nullopt);
  tracker.onCheck(t_instant);
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.0);
  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  tracker.onCheck(time_system.monotonicTime());
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 1.0);

  // Negative timeout clamps idle_time for that poll to 0ms (combined with a 20ms indefinite sleep
  // over 50ms total wall time so negative idle_time would produce 0.70 rather than 0.60).
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(-5));
  time_system.advanceTimeWait(std::chrono::milliseconds(5));
  tracker.onCheck(time_system.monotonicTime());
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(20));
  tracker.onCheck(time_system.monotonicTime());
  time_system.advanceTimeWait(std::chrono::milliseconds(25));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.60);
}

TEST(WorkerUtilizationTrackerTest, AccumulatesIdleTimeAcrossMultipleIterations) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Event::MockDispatcher> dispatcher;

  WorkerUtilizationTracker tracker(dispatcher);

  // Iteration 1: 20ms indefinite sleep (20ms idle), then 15ms active work.
  tracker.onPrepare(time_system.monotonicTime(), std::nullopt);
  time_system.advanceTimeWait(std::chrono::milliseconds(20));
  tracker.onCheck(time_system.monotonicTime());
  time_system.advanceTimeWait(std::chrono::milliseconds(15));

  // Iteration 2: 10ms-timeout sleep waking 20ms late at +30ms (10ms idle), then 35ms active work.
  // Total over 100ms window: 20ms + 10ms = 30ms idle -> 1.0 - 30 / 100 = 0.70.
  tracker.onPrepare(time_system.monotonicTime(), std::chrono::milliseconds(10));
  time_system.advanceTimeWait(std::chrono::milliseconds(30));
  tracker.onCheck(time_system.monotonicTime());
  time_system.advanceTimeWait(std::chrono::milliseconds(35));
  EXPECT_DOUBLE_EQ(tracker.sampleUtilization(), 0.70);
}

} // namespace
} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
