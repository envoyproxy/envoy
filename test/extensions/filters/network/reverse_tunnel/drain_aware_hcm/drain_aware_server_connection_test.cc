#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"

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

class DrainAwareServerConnectionTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  DrainAwareServerConnectionTest() {
    // MockTimer(dispatcher) registers itself as the next timer returned by createTimer_. The
    // wrapper creates its timer from the connection's dispatcher.
    timer_ = new Event::MockTimer(&connection_.dispatcher_);
    inner_ = std::make_unique<NiceMock<Http::MockServerConnection>>();
    inner_ptr_ = inner_.get();
    ON_CALL(*inner_ptr_, protocol()).WillByDefault(Return(Http::Protocol::Http2));
    ON_CALL(connection_, getSocket()).WillByDefault(ReturnRef(socket_));
  }

  // Creates the connection, consuming inner_. Expects the 100ms timer arm from the constructor.
  std::unique_ptr<DrainAwareServerConnection> makeConnection(bool drain_immediately = false) {
    EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
    return std::make_unique<DrainAwareServerConnection>(std::move(inner_), connection_,
                                                        drain_decision_, server_context_, nullptr,
                                                        nullptr, drain_immediately);
  }

  // Delivers a connection-level drain notification to the wrapper's registered callbacks.
  void raiseConnectionDrain(Server::DrainStrategy strategy = Server::DrainStrategy::Immediate) {
    connection_.raiseConnectionDrain(Network::ConnectionDrainEvent{
        connection_.dispatcher_.timeSource().monotonicTime(), strategy});
  }

  // Destroy conn cleanly, satisfying the disableTimer() call from the destructor.
  void destroyConnection(std::unique_ptr<DrainAwareServerConnection>& conn) {
    EXPECT_CALL(*timer_, disableTimer());
    conn.reset();
  }

  NiceMock<Network::MockConnection> connection_;
  Network::ConnectionSocketPtr socket_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
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
  EXPECT_OK(status);
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
// Legacy path: drain is detected by polling the DrainDecision.
TEST_F(DrainAwareServerConnectionTest, TimerFiresDrainDetected) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->invokeCallback();
  destroyConnection(conn);
}

// Equivalent using the connection-level drain path: the connection is notified via onDrain() and
// the timer detects the drain from that event (not by polling the DrainDecision).
TEST_F(DrainAwareServerConnectionTest, TimerFiresDrainDetectedViaConnectionDrain) {
  auto conn = makeConnection();
  // Notify the connection it is draining (Immediate strategy => drain right away). The
  // DrainDecision must not be consulted on this path.
  EXPECT_CALL(drain_decision_, drainClose(_)).Times(0);
  raiseConnectionDrain();
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->invokeCallback();
  destroyConnection(conn);
}

// After GOAWAY is sent, subsequent timer fires are complete no-ops.
TEST_F(DrainAwareServerConnectionTest, TimerFiresAfterGoAwaySentIsNoop) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  auto conn = makeConnection();
  // First fire: drain detected, GOAWAY sent. enabled_ is now false (no re-arm).
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*timer_, disableTimer());
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
  auto conn =
      std::make_unique<DrainAwareServerConnection>(std::move(inner_), connection_, drain_decision_,
                                                   server_context_, [&fired]() { fired = true; });
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  conn->shutdownNotice();
  EXPECT_TRUE(fired);
  destroyConnection(conn);
}

// on_local_drain fires at most once across shutdownNotice() and the drain timer.
TEST_F(DrainAwareServerConnectionTest, LocalDrainFiresAtMostOnce) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  int fired = 0;
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  auto conn = std::make_unique<DrainAwareServerConnection>(
      std::move(inner_), connection_, drain_decision_, server_context_, [&fired]() { ++fired; });
  conn->shutdownNotice();
  EXPECT_EQ(1, fired);

  // The drain timer also detects drain and emits the final GOAWAY, but the once-guard prevents a
  // second callback fire.
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, disableTimer());
  timer_->invokeCallback();
  EXPECT_EQ(1, fired);
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, ImmediateDrainSendsFinalGoAwayWithoutClosingConnection) {
  auto conn = makeConnection(true);
  EXPECT_CALL(connection_, close(_)).Times(0);
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, disableTimer());
  raiseConnectionDrain();
  EXPECT_EQ(Network::Connection::State::Open, connection_.state());

  // Streams already carried by the connection can still exchange data during the drain window.
  Buffer::OwnedImpl data("existing stream data");
  EXPECT_CALL(*inner_ptr_, dispatch(testing::Ref(data)));
  EXPECT_OK(conn->dispatch(data));
  raiseConnectionDrain();
  timer_->callback_();
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, ReplayedDrainSendsGoAwayBeforeFirstDispatch) {
  EXPECT_CALL(connection_, addConnectionCallbacks(_))
      .WillOnce([this](Network::ConnectionCallbacks& observer) {
        connection_.callbacks_.push_back(&observer);
        observer.onDrain({simTime().monotonicTime(), Server::DrainStrategy::Immediate});
      });
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  auto conn = makeConnection(true);
  testing::Mock::VerifyAndClearExpectations(inner_ptr_);

  Buffer::OwnedImpl data("new request");
  EXPECT_CALL(*timer_, disableTimer());
  {
    testing::InSequence sequence;
    EXPECT_CALL(*inner_ptr_, goAway());
    EXPECT_CALL(*inner_ptr_, dispatch(testing::Ref(data)));
  }
  EXPECT_OK(conn->dispatch(data));
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, ListenerDrainAndShutdownNoticeNotifyOnce) {
  int fired = 0;
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  auto conn = std::make_unique<DrainAwareServerConnection>(
      std::move(inner_), connection_, drain_decision_, server_context_, [&fired]() { ++fired; },
      nullptr, true);
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(connection_, close(_)).Times(0);
  raiseConnectionDrain();
  conn->shutdownNotice();
  conn->shutdownNotice();
  raiseConnectionDrain();
  EXPECT_EQ(1, fired);
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, ConnectionRotationRetainsReplacementGrace) {
  int fired = 0;
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  auto conn = std::make_unique<DrainAwareServerConnection>(
      std::move(inner_), connection_, drain_decision_, server_context_, [&fired]() { ++fired; },
      nullptr, true);
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  conn->shutdownNotice();
  EXPECT_EQ(1, fired);
  testing::Mock::VerifyAndClearExpectations(inner_ptr_);
  // HCM sends the final GOAWAY after the replacement grace window.
  EXPECT_CALL(*inner_ptr_, goAway());
  conn->goAway();
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, ImmediateObserverPreservesGradualStrategy) {
  ON_CALL(server_context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(600)));
  ON_CALL(server_context_.api_.random_, random()).WillByDefault(Return(599));
  auto conn = makeConnection(true);
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  raiseConnectionDrain(Server::DrainStrategy::Gradual);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  timer_->invokeCallback();
  testing::Mock::VerifyAndClearExpectations(inner_ptr_);
  simTime().advanceTimeWait(std::chrono::seconds(600));
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(connection_, close(_)).Times(0);
  timer_->invokeCallback();
  destroyConnection(conn);
}

TEST_F(DrainAwareServerConnectionTest, ClosedConnectionStopsPolling) {
  auto conn = makeConnection(true);
  EXPECT_CALL(*timer_, disableTimer()).Times(2);
  connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  timer_->callback_();
  conn->shutdownNotice();
  conn.reset();
  connection_.raiseConnectionDrain({simTime().monotonicTime(), Server::DrainStrategy::Immediate});
}

TEST_F(DrainAwareServerConnectionTest, LegacyDrainRespectsInboundDirection) {
  TestScopedRuntime runtime;
  runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  auto conn = std::make_unique<DrainAwareServerConnection>(
      std::move(inner_), connection_, drain_decision_, server_context_, nullptr, nullptr, true,
      Network::DrainDirection::InboundOnly);
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::InboundOnly))
      .WillOnce(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway());
  EXPECT_CALL(*timer_, disableTimer());
  timer_->invokeCallback();
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
