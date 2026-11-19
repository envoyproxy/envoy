#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
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

class DrainAwareServerConnectionTest : public testing::Test {
protected:
  DrainAwareServerConnectionTest() {
    inner_ = std::make_unique<NiceMock<Http::MockServerConnection>>();
    inner_ptr_ = inner_.get();
    ON_CALL(*inner_ptr_, protocol()).WillByDefault(Return(Http::Protocol::Http2));
  }

  // Creates the connection, consuming inner_. Mock timers are registered in reverse of the
  // wrapper's createTimer order because gmock matches the most recent expectation first.
  std::unique_ptr<DrainAwareServerConnection>
  makeConnection(std::function<void()> on_local_drain = nullptr,
                 DrainDelayConfig config = defaultConfig()) {
    goaway_timer_ = new Event::MockTimer(&connection_.dispatcher_);
    if (config.shutdown_notice_delay.count()) {
      shutdown_notice_timer_ = new Event::MockTimer(&connection_.dispatcher_);
    }
    if (config.metadata_delay.count()) {
      metadata_timer_ = new Event::MockTimer(&connection_.dispatcher_);
    }
    timer_ = new Event::MockTimer(&connection_.dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
    return std::make_unique<DrainAwareServerConnection>(std::move(inner_), connection_,
                                                        drain_decision_, server_context_, config,
                                                        std::move(on_local_drain));
  }

  static DrainDelayConfig defaultConfig() {
    return DrainDelayConfig{
        .shutdown_notice_delay = std::chrono::milliseconds(0),
        .metadata_delay = std::chrono::milliseconds(0),
        .goaway_delay = std::chrono::milliseconds(5000),
    };
  }

  static DrainDelayConfig delayConfig() {
    return DrainDelayConfig{
        .shutdown_notice_delay = std::chrono::milliseconds(3000),
        .metadata_delay = std::chrono::milliseconds(2000),
        .goaway_delay = std::chrono::milliseconds(5000),
    };
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
  NiceMock<Network::MockDrainDecision> drain_decision_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Event::MockTimer* timer_{nullptr};
  Event::MockTimer* metadata_timer_{nullptr};
  Event::MockTimer* shutdown_notice_timer_{nullptr};
  Event::MockTimer* goaway_timer_{nullptr};
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

// Timer fires when the listener is draining: sends the shutdown notice, then the GOAWAY after
// drain_timeout, and stops re-arming. Legacy path: drain is detected by polling the DrainDecision.
TEST_F(DrainAwareServerConnectionTest, TimerFiresDrainDetected) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, shutdownNotice());
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  EXPECT_CALL(*goaway_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->invokeCallback();

  EXPECT_CALL(*inner_ptr_, goAway());
  goaway_timer_->invokeCallback();
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
  EXPECT_CALL(*inner_ptr_, shutdownNotice());
  EXPECT_CALL(*goaway_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->invokeCallback();

  EXPECT_CALL(*inner_ptr_, goAway());
  goaway_timer_->invokeCallback();
  destroyConnection(conn);
}

// Once the drain has started, subsequent timer fires are complete no-ops.
TEST_F(DrainAwareServerConnectionTest, TimerFiresAfterDrainStartedIsNoop) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*goaway_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  timer_->invokeCallback();

  // MockTimer::invokeCallback() asserts enabled_ == true, so manually call the callback.
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*goaway_timer_, enableTimer(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->callback_();

  destroyConnection(conn);
}

// The GOAWAY is sent at most once across the HCM and the wrapper's GOAWAY timer.
TEST_F(DrainAwareServerConnectionTest, GoAwaySentAtMostOnce) {
  auto conn = makeConnection();
  EXPECT_CALL(*inner_ptr_, goAway());
  conn->goAway();
  conn->goAway();
  destroyConnection(conn);
}

// With an on_local_drain callback set, shutdownNotice() fires the callback and SUPPRESSES the
// inner shutdownNotice (the early GOAWAY), so the peer keeps using the tunnel during the grace
// window while a replacement is dialed.
TEST_F(DrainAwareServerConnectionTest,
       ShutdownNoticeWithLocalDrainFiresCallbackAndSuppressesInner) {
  bool fired = false;
  auto conn = makeConnection([&fired]() { fired = true; });
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
  auto conn = makeConnection([&fired]() { ++fired; });
  conn->shutdownNotice();
  EXPECT_EQ(1, fired);

  // The HCM already started the drain and owns the final GOAWAY, so the drain timer is a no-op.
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  EXPECT_CALL(*goaway_timer_, enableTimer(_, _)).Times(0);
  timer_->invokeCallback();
  EXPECT_EQ(1, fired);
  destroyConnection(conn);
}

// During a listener drain the HCM also drains on its own (on responses and proactively), so it can
// call shutdownNotice() after the wrapper has already started the drain. That second notice must
// not redial again, re-arm the drain timers or send the soft GOAWAY early.
TEST_F(DrainAwareServerConnectionTest, HcmShutdownNoticeAfterListenerDrainIsNoop) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  int redials = 0;
  auto conn = makeConnection([&redials]() { ++redials; }, delayConfig());

  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*metadata_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*shutdown_notice_timer_, enableTimer(std::chrono::milliseconds(3000), _));
  EXPECT_CALL(*goaway_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  timer_->invokeCallback();
  EXPECT_EQ(1, redials);

  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*metadata_timer_, enableTimer(_, _)).Times(0);
  EXPECT_CALL(*shutdown_notice_timer_, enableTimer(_, _)).Times(0);
  conn->shutdownNotice();
  EXPECT_EQ(1, redials) << "a second drain notice must not dial another replacement tunnel";
  destroyConnection(conn);
}

// Drain with delays: redial immediately, then the metadata frame, the soft GOAWAY and the final
// GOAWAY fire in order.
TEST_F(DrainAwareServerConnectionTest, DrainSendsMetadataThenShutdownNoticeThenGoAway) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  bool redialed = false;
  auto conn = makeConnection([&redialed]() { redialed = true; }, delayConfig());

  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway()).Times(0);
  EXPECT_CALL(*metadata_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*shutdown_notice_timer_, enableTimer(std::chrono::milliseconds(3000), _));
  EXPECT_CALL(*goaway_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  timer_->invokeCallback();
  EXPECT_TRUE(redialed);

  // Phase 1: metadata frame (client propagation).
  EXPECT_CALL(*inner_ptr_, encodeMetadata(_))
      .WillOnce([](const Http::MetadataMapVector& metadata_map_vector) {
        ASSERT_EQ(1, metadata_map_vector.size());
        EXPECT_EQ(Bootstrap::ReverseConnection::ReverseConnectionUtility::DRAIN_METADATA_VALUE,
                  (*metadata_map_vector[0])[std::string(
                      Bootstrap::ReverseConnection::ReverseConnectionUtility::DRAIN_METADATA_KEY)]);
      });
  metadata_timer_->invokeCallback();

  // Phase 2: soft GOAWAY (peer stops new streams).
  EXPECT_CALL(*inner_ptr_, shutdownNotice());
  shutdown_notice_timer_->invokeCallback();

  // Phase 3: final GOAWAY (close).
  EXPECT_CALL(*inner_ptr_, goAway());
  goaway_timer_->invokeCallback();
  destroyConnection(conn);
}

// shutdownNotice with metadata arms the metadata and shutdown-notice timers and still suppresses
// the inner notice at this point (it is deferred to the shutdown-notice timer).
TEST_F(DrainAwareServerConnectionTest, ShutdownNoticeWithMetadataArmsTimers) {
  bool redialed = false;
  auto conn = makeConnection([&redialed]() { redialed = true; }, delayConfig());
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*metadata_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*shutdown_notice_timer_, enableTimer(std::chrono::milliseconds(3000), _));
  conn->shutdownNotice();
  EXPECT_TRUE(redialed);

  EXPECT_CALL(*inner_ptr_, encodeMetadata(_));
  metadata_timer_->invokeCallback();

  EXPECT_CALL(*inner_ptr_, shutdownNotice());
  shutdown_notice_timer_->invokeCallback();
  destroyConnection(conn);
}

// Only the metadata delay is set: the metadata frame is sent and the soft GOAWAY is skipped.
TEST_F(DrainAwareServerConnectionTest, MetadataOnlySkipsShutdownNotice) {
  auto conn = makeConnection([]() {}, DrainDelayConfig{
                                          .shutdown_notice_delay = std::chrono::milliseconds(0),
                                          .metadata_delay = std::chrono::milliseconds(2000),
                                          .goaway_delay = std::chrono::milliseconds(5000),
                                      });
  EXPECT_CALL(*inner_ptr_, shutdownNotice()).Times(0);
  EXPECT_CALL(*metadata_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  conn->shutdownNotice();

  EXPECT_CALL(*inner_ptr_, encodeMetadata(_));
  metadata_timer_->invokeCallback();
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
