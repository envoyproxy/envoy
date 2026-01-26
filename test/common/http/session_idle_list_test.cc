#include "source/common/http/session_idle_list.h"

#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/time/time.h"
#include "envoy/event/timer.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

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
  explicit TestSessionIdleList(Event::Dispatcher& dispatcher)
      : SessionIdleList(dispatcher) {};

  // This type is neither copyable nor movable.
  TestSessionIdleList(const TestSessionIdleList&) = delete;
  TestSessionIdleList& operator=(const TestSessionIdleList&) = delete;

  using SessionIdleList::idle_sessions;
};

class SessionIdleListTest : public ::testing::Test {
 public:
  SessionIdleListTest() : idle_list_(dispatcher_) {
    auto sim_time = std::make_unique<Event::SimulatedTimeSystem>();
    time_system_ = sim_time.get();
    dispatcher_.time_system_ = std::move(sim_time);
    ON_CALL(dispatcher_, approximateMonotonicTime())
        .WillByDefault([this]() { return time_system_->monotonicTime(); });
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

TEST_F(SessionIdleListTest, TerminateIdleSessionsUpToMax) {
  idle_list_.set_max_sessions_to_terminate_in_one_round(1);
  idle_list_.set_min_time_before_purge_allowed(absl::Seconds(1));
  TestIdleSession session1;
  idle_list_.AddSession(session1);
  time_system_->advanceTimeWait(std::chrono::seconds(2));
  idle_list_.MaybeTerminateIdleSessions();
  EXPECT_TRUE(session1.is_closed());
  EXPECT_FALSE(idle_list_.idle_sessions()->ContainsForTest(session1));
}

TEST_F(SessionIdleListTest, TerminateIdleSessionsByOrder) {
  idle_list_.set_max_sessions_to_terminate_in_one_round(1);
  idle_list_.set_min_time_before_purge_allowed(absl::Seconds(1));
  TestIdleSession session1, session2, session3;
  idle_list_.AddSession(session1);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.AddSession(session2);
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.AddSession(session3);
  time_system_->advanceTimeWait(std::chrono::seconds(1));

  // Terminate the oldest session, which is session1.
  idle_list_.MaybeTerminateIdleSessions();
  EXPECT_TRUE(session1.is_closed());
  EXPECT_FALSE(session2.is_closed());
  EXPECT_FALSE(session3.is_closed());

  // Refresh session2's enqueue time, so it becomes the newest.
  time_system_->advanceTimeWait(std::chrono::seconds(1));
  idle_list_.RemoveSession(session2);
  idle_list_.AddSession(session2);

  idle_list_.MaybeTerminateIdleSessions();
  EXPECT_TRUE(session3.is_closed());
  EXPECT_FALSE(session2.is_closed());
  EXPECT_TRUE(idle_list_.idle_sessions()->ContainsForTest(session2));
}

}  // namespace Http
}  // namespace Envoy
