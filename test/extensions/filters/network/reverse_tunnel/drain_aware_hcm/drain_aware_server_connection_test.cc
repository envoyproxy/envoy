#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

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

// With an on_local_drain callback set, shutdownNotice() fires the callback and SUPPRESSES the
// inner shutdownNotice (the early GOAWAY), so the peer keeps using the tunnel during the grace
// window while a replacement is dialed.
TEST_F(DrainAwareServerConnectionTest,
       ShutdownNoticeWithLocalDrainFiresCallbackAndSuppressesInner) {
  bool fired = false;
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  auto conn = std::make_unique<DrainAwareServerConnection>(
      std::move(inner_), dispatcher_, drain_decision_, [&fired]() { fired = true; });
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  conn->shutdownNotice();
  EXPECT_TRUE(fired);
  destroyConnection(conn);
}

// on_local_drain fires at most once across shutdownNotice() and the drain timer.
TEST_F(DrainAwareServerConnectionTest, LocalDrainFiresAtMostOnce) {
  int fired = 0;
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  auto conn = std::make_unique<DrainAwareServerConnection>(
      std::move(inner_), dispatcher_, drain_decision_, [&fired]() { ++fired; });
  conn->shutdownNotice();
  EXPECT_EQ(1, fired);

  // The drain timer also detects drain and emits the final GOAWAY, but the once-guard prevents a
  // second callback fire.
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway());
  timer_->invokeCallback();
  EXPECT_EQ(1, fired);
  destroyConnection(conn);
}

// Tests for the peer-GOAWAY interceptor that sits between the codec and the HCM callbacks.
class DrainAwareServerConnectionCallbacksTest : public testing::Test {
protected:
  NiceMock<Http::MockServerConnectionCallbacks> inner_callbacks_;
};

// A received GOAWAY fires the re-dial closure exactly once and still delegates to the inner
// callbacks; a second GOAWAY delegates but does not re-fire the closure.
TEST_F(DrainAwareServerConnectionCallbacksTest, PeerGoAwayFiresClosureOnceAndDelegates) {
  int fired = 0;
  DrainAwareServerConnectionCallbacks wrapper(inner_callbacks_, [&fired]() { ++fired; });

  EXPECT_CALL(inner_callbacks_, onGoAway(Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Http::GoAwayErrorCode::NoError);
  EXPECT_EQ(1, fired);

  EXPECT_CALL(inner_callbacks_, onGoAway(Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Http::GoAwayErrorCode::NoError);
  EXPECT_EQ(1, fired);
}

// A null closure (peer-GOAWAY re-dial disabled) just delegates onGoAway.
TEST_F(DrainAwareServerConnectionCallbacksTest, NullClosureJustDelegatesGoAway) {
  DrainAwareServerConnectionCallbacks wrapper(inner_callbacks_, nullptr);
  EXPECT_CALL(inner_callbacks_, onGoAway(Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Http::GoAwayErrorCode::NoError);
}

// newStream passes through to the inner callbacks unchanged.
TEST_F(DrainAwareServerConnectionCallbacksTest, NewStreamDelegates) {
  DrainAwareServerConnectionCallbacks wrapper(inner_callbacks_, nullptr);
  NiceMock<Http::MockResponseEncoder> encoder;
  NiceMock<Http::MockRequestDecoder> decoder;
  EXPECT_CALL(inner_callbacks_, newStream(_, false)).WillOnce(ReturnRef(decoder));
  EXPECT_EQ(&decoder, &wrapper.newStream(encoder, false));
}

// onSettings passes through to the inner callbacks unchanged.
TEST_F(DrainAwareServerConnectionCallbacksTest, OnSettingsDelegates) {
  DrainAwareServerConnectionCallbacks wrapper(inner_callbacks_, nullptr);
  NiceMock<Http::MockReceivedSettings> settings;
  EXPECT_CALL(inner_callbacks_, onSettings(_));
  wrapper.onSettings(settings);
}

// onMaxStreamsChanged passes through to the inner callbacks unchanged. (onMaxStreamsChanged has a
// default interface implementation, so it is not a gmock method; exercising the forwarding path is
// enough for coverage.)
TEST_F(DrainAwareServerConnectionCallbacksTest, OnMaxStreamsChangedDelegates) {
  DrainAwareServerConnectionCallbacks wrapper(inner_callbacks_, nullptr);
  wrapper.onMaxStreamsChanged(7);
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
