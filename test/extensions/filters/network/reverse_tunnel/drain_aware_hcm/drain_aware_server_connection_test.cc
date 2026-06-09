#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/io_handle.h"
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

constexpr std::chrono::milliseconds kDrainTimeout{5000};
constexpr std::chrono::milliseconds kPollInterval{100};

class DrainAwareServerConnectionTest : public testing::Test {
protected:
  DrainAwareServerConnectionTest() {
    // The wrapper's constructor calls createTimer twice: first for the drain-poll timer, then for
    // the proactive-goaway timer. MockTimer expectations are matched LIFO, so declare the
    // proactive mock first and the poll mock second to map them to the right createTimer calls.
    proactive_goaway_timer_ = new Event::MockTimer(&dispatcher_);
    drain_check_timer_ = new Event::MockTimer(&dispatcher_);
    inner_ = std::make_unique<NiceMock<Http::MockServerConnection>>();
    inner_ptr_ = inner_.get();
    ON_CALL(*inner_ptr_, protocol()).WillByDefault(Return(Http::Protocol::Http2));
  }

  std::unique_ptr<DrainAwareServerConnection> makeConnection() {
    EXPECT_CALL(*drain_check_timer_, enableTimer(kPollInterval, _));
    return std::make_unique<DrainAwareServerConnection>(std::move(inner_), drain_decision_,
                                                        io_handle_, dispatcher_, kDrainTimeout);
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
  NiceMock<Network::MockIoHandle> io_handle_;
  Event::MockTimer* drain_check_timer_{nullptr};
  Event::MockTimer* proactive_goaway_timer_{nullptr};
  std::unique_ptr<NiceMock<Http::MockServerConnection>> inner_;
  NiceMock<Http::MockServerConnection>* inner_ptr_{nullptr};
};

// --- Delegation ---

TEST_F(DrainAwareServerConnectionTest, DelegatesDispatch) {
  auto conn = makeConnection();
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*inner_ptr_, dispatch(testing::Ref(data)));
  auto status = conn->dispatch(data);
  EXPECT_TRUE(status.ok());
}

TEST_F(DrainAwareServerConnectionTest, DelegatesProtocol) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, protocol()).WillOnce(Return(Http::Protocol::Http2));
  EXPECT_EQ(Http::Protocol::Http2, conn->protocol());
}

TEST_F(DrainAwareServerConnectionTest, DelegatesWantsToWrite) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, wantsToWrite()).WillOnce(Return(true));
  EXPECT_TRUE(conn->wantsToWrite());
}

TEST_F(DrainAwareServerConnectionTest, DelegatesAboveWriteBufferHighWatermark) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  conn->onUnderlyingConnectionAboveWriteBufferHighWatermark();
}

TEST_F(DrainAwareServerConnectionTest, DelegatesBelowWriteBufferLowWatermark) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, onUnderlyingConnectionBelowWriteBufferLowWatermark());
  conn->onUnderlyingConnectionBelowWriteBufferLowWatermark();
}

// --- goAway() guard ---

// goAway() forwards to the inner codec exactly once; subsequent calls are no-ops.
TEST_F(DrainAwareServerConnectionTest, GoAwayForwardsOnce) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, goAway());
  conn->goAway();
  conn->goAway();
}

// --- shutdownNotice(): always swallowed (never forwarded to inner) ---

// Connection-level drain (listener healthy): swallow phase-1 GOAWAY and reconnect. With
// MockIoHandle the dynamic_cast yields nullptr, so reconnection is a no-op (and must not crash).
TEST_F(DrainAwareServerConnectionTest, ShutdownNoticeConnectionDrainSwallowsAndReconnects) {
  auto conn = makeConnection();
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::All)).WillOnce(Return(false));
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  conn->shutdownNotice();
}

// Listener-level drain: swallow phase-1 GOAWAY and do NOT reconnect (listener is dying).
TEST_F(DrainAwareServerConnectionTest, ShutdownNoticeListenerDrainSwallowsNoReconnect) {
  auto conn = makeConnection();
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::All)).WillOnce(Return(true));
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  conn->shutdownNotice();
}

// shutdownNotice() is idempotent: the second call short-circuits before querying drainClose().
TEST_F(DrainAwareServerConnectionTest, ShutdownNoticeIsIdempotent) {
  auto conn = makeConnection();
  EXPECT_CALL(drain_decision_, drainClose(_)).WillOnce(Return(false));
  conn->shutdownNotice();
  conn->shutdownNotice();
}

// --- Polling path (listener-level drain on an idle connection) ---

// Timer fires, listener not draining: re-arm the poll timer, take no drain action.
TEST_F(DrainAwareServerConnectionTest, PollTimerNoDrainReArms) {
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(false));
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  EXPECT_CALL(*drain_check_timer_, enableTimer(kPollInterval, _));
  drain_check_timer_->invokeCallback();
}

// Timer fires, listener draining: arm the proactive-goaway timer for drain_timeout and stop
// re-arming the poll timer. No GOAWAY yet; reconnection is not attempted.
TEST_F(DrainAwareServerConnectionTest, PollTimerDrainDetectedSchedulesGoAway) {
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));

  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  EXPECT_CALL(*drain_check_timer_, enableTimer(_, _)).Times(0);
  EXPECT_CALL(*proactive_goaway_timer_, enableTimer(kDrainTimeout, _));
  drain_check_timer_->invokeCallback();

  // When the proactive timer fires, the final GOAWAY is sent.
  EXPECT_CALL(*inner_ptr_, goAway());
  proactive_goaway_timer_->invokeCallback();
}

// Once a drain has been initiated, a subsequent poll-timer fire is a no-op.
TEST_F(DrainAwareServerConnectionTest, PollTimerAfterDrainInitiatedIsNoop) {
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(false));
  conn->shutdownNotice(); // sets drain_initiated_

  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*drain_check_timer_, enableTimer(_, _)).Times(0);
  // The poll timer was last enabled by the constructor; invoke its callback directly.
  drain_check_timer_->invokeCallback();
}

// The goAway guard prevents a duplicate GOAWAY across both paths: if the HCM sends goAway and the
// proactive timer later fires (or vice versa), the inner codec only sees a single GOAWAY.
TEST_F(DrainAwareServerConnectionTest, GoAwayDedupAcrossHcmAndProactiveTimer) {
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));

  // Poll detects listener drain → proactive-goaway timer armed.
  EXPECT_CALL(*proactive_goaway_timer_, enableTimer(kDrainTimeout, _));
  drain_check_timer_->invokeCallback();

  // The inner codec must see exactly one goAway across both triggers.
  EXPECT_CALL(*inner_ptr_, goAway());
  conn->goAway();                            // e.g. HCM-driven
  proactive_goaway_timer_->invokeCallback(); // proactive timer
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
