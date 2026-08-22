#include <chrono>

#include "source/common/config/ttl.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class TtlManagerTest : public testing::Test {
public:
  TtlManagerTest() { test_time_.setSystemTime(std::chrono::milliseconds(0)); }
  Event::MockDispatcher dispatcher_;
  Event::SimulatedTimeSystem test_time_;
};

TEST_F(TtlManagerTest, BasicUsage) {
  std::optional<std::vector<std::string>> maybe_expired;
  auto cb = [&](const auto& expired) { maybe_expired = expired; };
  auto ttl_timer = new Event::MockTimer(&dispatcher_);
  TtlManager ttl(cb, dispatcher_, dispatcher_.timeSource());

  EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(1), _));
  EXPECT_CALL(*ttl_timer, enabled());
  ttl.add(std::chrono::milliseconds(1), "hello");

  // Adding another expiration after the first one should not update the timer.
  EXPECT_CALL(*ttl_timer, enabled());
  ttl.add(std::chrono::milliseconds(5), "not hello");

  // Advance time by 3 ms, this should only trigger the first TTL.
  test_time_.setSystemTime(std::chrono::milliseconds(3));
  EXPECT_CALL(*ttl_timer, enabled());
  EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(2), _));
  ttl_timer->invokeCallback();
  EXPECT_EQ(maybe_expired.value(), std::vector<std::string>({"hello"}));

  // Add in a TTL entry that comes before the scheduled timer run, this should update the timer.
  EXPECT_CALL(*ttl_timer, enabled());
  EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(1), _));
  ttl.add(std::chrono::milliseconds(1), "hello");

  // Clearing the first TTL entry should reset the timer to match the next in line.
  EXPECT_CALL(*ttl_timer, enabled());
  EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(2), _));
  ttl.clear("hello");

  // Removing all the TTLs should disable the timer.
  EXPECT_CALL(*ttl_timer, disableTimer());
  ttl.clear("not hello");
}

TEST_F(TtlManagerTest, ScopedUpdate) {
  std::optional<std::vector<std::string>> maybe_expired;
  auto cb = [&](const auto& expired) { maybe_expired = expired; };
  auto ttl_timer = new Event::MockTimer(&dispatcher_);
  TtlManager ttl(cb, dispatcher_, dispatcher_.timeSource());

  {
    const auto scoped = ttl.scopedTtlUpdate();
    ttl.add(std::chrono::milliseconds(1), "hello");
    ttl.add(std::chrono::milliseconds(5), "not hello");

    // There should only be a single update due to the scoping.
    EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(1), _));
    EXPECT_CALL(*ttl_timer, enabled());
  }

  {
    const auto scoped = ttl.scopedTtlUpdate();
    ttl.clear("hello");
    ttl.clear("not hello");

    // There should only be a single update due to the scoping.
    EXPECT_CALL(*ttl_timer, disableTimer());
  }
}

TEST_F(TtlManagerTest, OverdueTtl) {
  std::optional<std::vector<std::string>> maybe_expired;
  auto cb = [&](const auto& expired) {
    maybe_expired = expired;
    // Simulate time passing during callback past the next item's expiry.
    test_time_.setMonotonicTime(std::chrono::milliseconds(10));
  };
  auto ttl_timer = new Event::MockTimer(&dispatcher_);
  TtlManager ttl(cb, dispatcher_, dispatcher_.timeSource());

  EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(1), _));
  EXPECT_CALL(*ttl_timer, enabled());
  ttl.add(std::chrono::milliseconds(1), "first");

  EXPECT_CALL(*ttl_timer, enabled());
  ttl.add(std::chrono::milliseconds(5), "second");

  // Advance time to 1 ms to trigger the first TTL.
  test_time_.setMonotonicTime(std::chrono::milliseconds(1));
  EXPECT_CALL(*ttl_timer, enabled());
  // The callback will advance time to 10ms (past "second"'s expiry of 5ms).
  // The timer should be re-enabled with 0ms instead of a negative duration.
  EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(0), _));
  ttl_timer->invokeCallback();
  EXPECT_EQ(maybe_expired.value(), std::vector<std::string>({"first"}));

  // Now invoke the callback again for the overdue "second" entry.
  EXPECT_CALL(*ttl_timer, disableTimer());
  ttl_timer->invokeCallback();
  EXPECT_EQ(maybe_expired.value(), std::vector<std::string>({"second"}));
}

} // namespace
} // namespace Config
} // namespace Envoy
