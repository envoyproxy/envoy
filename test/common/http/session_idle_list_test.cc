#include <chrono>
#include <memory>
#include <utility>

#include "envoy/event/timer.h"

#include "source/common/http/session_idle_list.h"
#include "source/common/http/session_idle_list_interface.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class TestIdleSession : public IdleSessionInterface {
public:
  TestIdleSession() = default;
  virtual ~TestIdleSession() = default;

  void TerminateIdleSession() override { is_closed_ = true; }

  bool is_closed() const { return is_closed_; }

private:
  bool is_closed_ = false;
};

class TestSessionIdleList : public SessionIdleList {
public:
  explicit TestSessionIdleList(Event::Dispatcher& dispatcher) : SessionIdleList(dispatcher) {};

  // This type is neither copyable nor movable.
  TestSessionIdleList(const TestSessionIdleList&) = delete;
  TestSessionIdleList& operator=(const TestSessionIdleList&) = delete;

  using SessionIdleList::idle_sessions;
  using SessionIdleList::MinTimeBeforeTerminationAllowed;
};

class SessionIdleListTest : public ::testing::Test {
public:
  SessionIdleListTest() : idle_list_(dispatcher_) {
    auto sim_time = std::make_unique<Event::SimulatedTimeSystem>();
    time_system_ = sim_time.get();
    dispatcher_.time_system_ = std::move(sim_time);
    ON_CALL(dispatcher_, approximateMonotonicTime()).WillByDefault([this]() {
      return time_system_->monotonicTime();
    });
    idle_list_.set_min_time_before_termination_allowed(absl::Minutes(1));
    idle_list_.set_max_sessions_to_terminate_in_one_round(1);
    idle_list_.set_max_sessions_to_terminate_in_one_round_when_saturated(2);
  }

protected:
  Event::SimulatedTimeSystem* time_system_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  TestSessionIdleList idle_list_;
};

TEST_F(SessionIdleListTest, AddRemoveSession) {
  TestIdleSession session1, session2;
  idle_list_.AddSession(session1);
  idle_list_.AddSession(session2);
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session1));
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session2));

  idle_list_.RemoveSession(session1);
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session1));
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session2));
}

TEST_F(SessionIdleListTest, TerminateIdleSessionsUpToMaxSessionsAllowed) {
  TestIdleSession session1, session2;
  idle_list_.AddSession(session1);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.AddSession(session2);
  EXPECT_EQ(idle_list_.idle_sessions()->size(), 2);
  time_system_->advanceTimeWait(std::chrono::minutes(1));
  idle_list_.MaybeTerminateIdleSessions(/*is_saturated=*/false);
  EXPECT_TRUE(session1.is_closed());
  EXPECT_FALSE(session2.is_closed());
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session1));
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session2));
  EXPECT_EQ(idle_list_.idle_sessions()->size(), 1);
}

TEST_F(SessionIdleListTest, RespectMinTimeBeforeTermination) {
  TestIdleSession session1;
  idle_list_.AddSession(session1);
  time_system_->advanceTimeWait(std::chrono::seconds(59));
  idle_list_.MaybeTerminateIdleSessions(/*is_saturated=*/false);
  EXPECT_FALSE(session1.is_closed());
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session1));

  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.MaybeTerminateIdleSessions(/*is_saturated=*/false);
  EXPECT_TRUE(session1.is_closed());
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session1));
}

TEST_F(SessionIdleListTest, TerminateMultipleSessionsByOrder) {
  idle_list_.set_max_sessions_to_terminate_in_one_round(2);
  TestIdleSession session1, session2, session3;
  idle_list_.AddSession(session1);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.AddSession(session2);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.AddSession(session3);

  time_system_->advanceTimeWait(std::chrono::minutes(1));

  // s1 is oldest, then s2, then s3.
  // max_sessions_to_terminate_in_one_round = 2, so s1 and s2 should be
  // terminated.
  idle_list_.MaybeTerminateIdleSessions(/*is_saturated=*/false);
  EXPECT_TRUE(session1.is_closed());
  EXPECT_TRUE(session2.is_closed());
  EXPECT_FALSE(session3.is_closed());
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session1));
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session2));
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session3));
}

TEST_F(SessionIdleListTest, TerminateIdleSessionsWhenOverloaded) {
  // When overloaded, we should ignore min_time_before_termination_allowed_ and
  // terminate up to max_sessions_to_terminate_in_one_round_when_overload_
  // sessions.
  idle_list_.set_ignore_min_time_before_termination_allowed(true);
  TestIdleSession session1, session2, session3;
  idle_list_.AddSession(session1);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.AddSession(session2);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.AddSession(session3);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.MaybeTerminateIdleSessions(/*is_saturated=*/true);
  // When ignore_min_time_before_termination_allowed_ is true, we should
  // terminate up to max_sessions_to_terminate_in_one_round_when_overload_
  // sessions, which is 2.
  EXPECT_TRUE(session1.is_closed());
  EXPECT_TRUE(session2.is_closed());
  EXPECT_FALSE(session3.is_closed());
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session1));
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session2));
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session3));
  EXPECT_EQ(idle_list_.idle_sessions()->size(), 1);
}

TEST_F(SessionIdleListTest, MinTimeBeforeTerminationAllowed) {
  EXPECT_EQ(idle_list_.MinTimeBeforeTerminationAllowed(), absl::Minutes(1));
}

TEST_F(SessionIdleListTest, RemoveNonExistentSession) {
  TestIdleSession session1, session2;
  idle_list_.AddSession(session1);
  // Should not crash or bug.
  idle_list_.RemoveSession(session2);
  EXPECT_EQ(idle_list_.idle_sessions()->size(), 1);
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session1));
}

TEST_F(SessionIdleListTest, MaybeTerminateIdleSessionsEmptyList) {
  // Should not crash or bug.
  idle_list_.MaybeTerminateIdleSessions(/*is_saturated=*/false);
  idle_list_.MaybeTerminateIdleSessions(/*is_saturated=*/true);
  EXPECT_EQ(idle_list_.idle_sessions()->size(), 0);
}

TEST_F(SessionIdleListTest, DuplicateAddSessionBug) {
  TestIdleSession session1;
  idle_list_.AddSession(session1);
  EXPECT_ENVOY_BUG(idle_list_.AddSession(session1), "Session is already on the idle list.");
}

TEST_F(SessionIdleListTest, GetEnqueueTimeBug) {
  TestIdleSession session1;
  EXPECT_ENVOY_BUG(idle_list_.idle_sessions()->GetEnqueueTime(session1),
                   "Attempt to get enqueue time for session which is not in the idle set.");
}

} // namespace Http
} // namespace Envoy
