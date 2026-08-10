#include <string>
#include <vector>

#include "source/extensions/filters/network/redis_proxy/subscribe_ack_deadline_scheduler.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;

class SubscribeAckDeadlineSchedulerTest : public testing::Test {
protected:
  SubscribeAckDeadlineSchedulerTest() {
    // Capture the single timer the scheduler lazily creates so tests can fire it by hand.
    ON_CALL(dispatcher_, createTimer_(_)).WillByDefault(Invoke([this](Event::TimerCb cb) {
      timer_cb_ = cb;
      timer_ = new NiceMock<Event::MockTimer>();
      return timer_;
    }));
  }

  SubscribeAckDeadlineScheduler makeScheduler() {
    return SubscribeAckDeadlineScheduler(
        dispatcher_, std::chrono::milliseconds(10000),
        [this](const std::string& channel, uint64_t seq) {
          auto it = live_.find(channel);
          return it != live_.end() && it->second == seq;
        },
        [this](const std::string& channel) { expired_.push_back(channel); });
  }

  // Declared first so it installs the frozen (simulated) global clock before the dispatcher reads
  // it: without this, MockDispatcher's default time source advances between schedule() calls,
  // giving each channel a distinct deadline (only the front would drain on one fire). In production
  // a batch SUBSCRIBE registers every channel at the SAME event-loop instant, so they share one
  // deadline.
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* timer_{nullptr}; // owned by the scheduler; captured on createTimer
  Event::TimerCb timer_cb_;
  absl::flat_hash_map<std::string, uint64_t> live_; // channel -> the seq currently "live"
  std::vector<std::string> expired_;
};

// schedule() hands out monotonically increasing tokens (the bucket stores one to prove its
// identity).
TEST_F(SubscribeAckDeadlineSchedulerTest, ScheduleAssignsMonotonicTokens) {
  auto scheduler = makeScheduler();
  EXPECT_EQ(0U, scheduler.schedule("a"));
  EXPECT_EQ(1U, scheduler.schedule("b"));
  EXPECT_EQ(2U, scheduler.schedule("c"));
}

// The timer is created + armed only on the empty -> non-empty edge (later entries ride the earlier
// deadline).
TEST_F(SubscribeAckDeadlineSchedulerTest, ArmsTimerOnlyOnFirstSchedule) {
  auto scheduler = makeScheduler();
  scheduler.schedule("a");
  ASSERT_NE(nullptr, timer_); // created on the first schedule
  // A second schedule while the queue is non-empty must NOT re-create or re-arm the timer.
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  scheduler.schedule("b");
}

// On fire, every leading entry sharing the fired deadline is drained; live ones fire on_expired in
// order, stale ones (bucket acked/dropped, so not "live") are skipped.
TEST_F(SubscribeAckDeadlineSchedulerTest, FireExpiresLiveEntriesInOrderAndSkipsStale) {
  auto scheduler = makeScheduler();
  live_["a"] = scheduler.schedule("a");
  scheduler.schedule("b"); // NOT recorded live -> stale on fire (its bucket acked/dropped)
  live_["c"] = scheduler.schedule("c");
  ASSERT_NE(nullptr, timer_cb_);
  timer_cb_(); // all three registered under the same constant deadline, so one fire drains them
  EXPECT_THAT(expired_, testing::ElementsAre("a", "c"));
}

// pruneAndRearm drops dead leading entries (an acked bucket left its entry behind) so the timer
// tracks a real deadline instead of a token-miss no-op.
TEST_F(SubscribeAckDeadlineSchedulerTest, PruneAndRearmDropsDeadLeadingThenFireHitsOnlyLive) {
  auto scheduler = makeScheduler();
  scheduler.schedule("dead"); // never marked live
  live_["live"] = scheduler.schedule("live");
  scheduler.pruneAndRearm(); // drops the dead leading "dead" entry
  ASSERT_NE(nullptr, timer_cb_);
  timer_cb_();
  EXPECT_THAT(expired_, testing::ElementsAre("live"));
}

// clear() empties the queue and disables the timer (registry clear() on cluster removal).
TEST_F(SubscribeAckDeadlineSchedulerTest, ClearDisablesTimer) {
  auto scheduler = makeScheduler();
  live_["a"] = scheduler.schedule("a");
  ASSERT_NE(nullptr, timer_);
  EXPECT_CALL(*timer_, disableTimer());
  scheduler.clear();
  // After clear the queue is empty, so a fire is a no-op (no on_expired).
  timer_cb_();
  EXPECT_TRUE(expired_.empty());
}

} // namespace
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
