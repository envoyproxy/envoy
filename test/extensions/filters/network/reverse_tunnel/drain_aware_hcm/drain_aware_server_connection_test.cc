#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

class DrainAwareServerConnectionTest : public testing::Test {
protected:
  DrainAwareServerConnectionTest() {
    // MockTimer(dispatcher) registers itself as the next timer returned by createTimer_.
    timer_ = new Event::MockTimer(&dispatcher_);
    inner_ = std::make_unique<NiceMock<Http::MockServerConnection>>();
    inner_ptr_ = inner_.get();
    ON_CALL(*inner_ptr_, protocol()).WillByDefault(Return(Http::Protocol::Http2));
  }

  // Creates the connection, consuming inner_. Expects the 100ms timer arm from the constructor.
  std::unique_ptr<DrainAwareServerConnection> makeConnection() {
    EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
    return std::make_unique<DrainAwareServerConnection>(std::move(inner_), dispatcher_,
                                                        drain_decision_);
  }

  // Destroy conn cleanly, satisfying the disableTimer() call from the destructor.
  void destroyConnection(std::unique_ptr<DrainAwareServerConnection>& conn) {
    EXPECT_CALL(*timer_, disableTimer());
    conn.reset();
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
  Event::MockTimer* timer_{nullptr};
  std::unique_ptr<NiceMock<Http::MockServerConnection>> inner_;
  NiceMock<Http::MockServerConnection>* inner_ptr_{nullptr};
};

// Constructor arms the 100ms drain-check timer.
TEST_F(DrainAwareServerConnectionTest, ConstructorStartsTimer) {
  auto conn = makeConnection();
  destroyConnection(conn);
}

// Destructor disables the timer.
TEST_F(DrainAwareServerConnectionTest, DestructorDisablesTimer) {
  auto conn = makeConnection();
  EXPECT_CALL(*timer_, disableTimer());
  conn.reset();
}

// All delegating methods forward to the inner connection.
TEST_F(DrainAwareServerConnectionTest, DelegatesDispatch) {
  auto conn = makeConnection();
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*inner_ptr_, dispatch(testing::Ref(data)));
  auto status = conn->dispatch(data);
  EXPECT_TRUE(status.ok());
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, DelegatesGoAway) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, goAway());
  conn->goAway();
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, DelegatesProtocol) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, protocol()).WillOnce(Return(Http::Protocol::Http2));
  EXPECT_EQ(Http::Protocol::Http2, conn->protocol());
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, DelegatesShutdownNotice) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, shutdownNotice());
  conn->shutdownNotice();
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, DelegatesWantsToWrite) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, wantsToWrite()).WillOnce(Return(true));
  EXPECT_TRUE(conn->wantsToWrite());
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, DelegatesAboveWriteBufferHighWatermark) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  conn->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, DelegatesBelowWriteBufferLowWatermark) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, onUnderlyingConnectionBelowWriteBufferLowWatermark());
  conn->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  destroyConnection(conn);
}

// Timer fires when the listener is not draining: re-arms the timer, no GOAWAY.
TEST_F(DrainAwareServerConnectionTest, TimerFiresNoDrain) {
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(false));
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  timer_->invokeCallback();
  destroyConnection(conn);
}

// Timer fires when the listener is draining: sends GOAWAY once, stops re-arming.
TEST_F(DrainAwareServerConnectionTest, TimerFiresDrainDetected) {
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->invokeCallback();
  destroyConnection(conn);
}

// After GOAWAY is sent, subsequent timer fires are complete no-ops.
TEST_F(DrainAwareServerConnectionTest, TimerFiresAfterGoAwaySentIsNoop) {
  auto conn = makeConnection();
  // First fire: drain detected, GOAWAY sent. enabled_ is now false (no re-arm).
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  timer_->invokeCallback();

  // Second fire: drain_goaway_sent_ == true, early return before any calls.
  // MockTimer::invokeCallback() asserts enabled_ == true, so manually call the callback.
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->callback_();

  destroyConnection(conn);
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
