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
    // MockTimer(dispatcher) registers an EXPECT_CALL on createTimer_. Gmock matches the most
    // recently registered expectation first, so these are constructed in reverse of the wrapper
    // constructor's createTimer order: drain-check, then metadata, then shutdown-notice, then
    // GOAWAY.
    goaway_timer_ = new Event::MockTimer(&connection_.dispatcher_);
    shutdown_notice_timer_ = new Event::MockTimer(&connection_.dispatcher_);
    metadata_timer_ = new Event::MockTimer(&connection_.dispatcher_);
    timer_ = new Event::MockTimer(&connection_.dispatcher_);
    inner_ = std::make_unique<NiceMock<Http::MockServerConnection>>();
    inner_ptr_ = inner_.get();
    ON_CALL(*inner_ptr_, protocol()).WillByDefault(Return(Http::Protocol::Http2));
  }

  // Creates the connection, consuming inner_. Expects the 100ms drain-check timer arm.
  std::unique_ptr<DrainAwareServerConnection>
  makeConnection(std::function<void()> on_local_drain = nullptr,
                 std::shared_ptr<ConnectionMetadataConfig> metadata_config = nullptr) {
    EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
    return std::make_unique<DrainAwareServerConnection>(
        std::move(inner_), connection_, drain_decision_, server_context_, std::move(on_local_drain),
        nullptr, std::move(metadata_config));
  }

  std::shared_ptr<ConnectionMetadataConfig> makeMetadataConfig(
      std::chrono::milliseconds delay = std::chrono::milliseconds(2000),
      std::chrono::milliseconds shutdown_notice_delay = std::chrono::milliseconds(3000),
      std::chrono::milliseconds goaway_delay = std::chrono::milliseconds(5000),
      const std::string& key = "drain_reverse_tunnel") {
    return std::make_shared<ConnectionMetadataConfig>(
        ConnectionMetadataConfig{key, delay, shutdown_notice_delay, goaway_delay});
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

// Timer fires when the listener is draining: sends GOAWAY once, stops re-arming.
// Legacy path: drain is detected by polling the DrainDecision.
TEST_F(DrainAwareServerConnectionTest, TimerFiresDrainDetected) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  auto conn = makeConnection();
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway());
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

  // The drain timer also detects drain and emits the final GOAWAY, but the once-guard prevents a
  // second callback fire.
  ON_CALL(drain_decision_, drainClose(_)).WillByDefault(Return(true));
  EXPECT_CALL(*inner_ptr_, goAway());
  timer_->invokeCallback();
  EXPECT_EQ(1, fired);
  destroyConnection(conn);
}

// Drain with connection metadata: redial immediately, then three phases fire in order: metadata
// frame after `delay` (client propagation), soft GOAWAY after `shutdown_notice_delay` (peer stops
// new streams), final GOAWAY after `goaway_delay` (close).
TEST_F(DrainAwareServerConnectionTest, DrainSendsMetadataThenShutdownNoticeThenGoAway) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  bool redialed = false;
  auto conn = makeConnection([&redialed]() { redialed = true; }, makeMetadataConfig());

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
        EXPECT_EQ("true", (*metadata_map_vector[0])["drain_reverse_tunnel"]);
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
  auto conn = makeConnection([&redialed]() { redialed = true; }, makeMetadataConfig());
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

// An empty metadata key still arms the timers (arming is gated on metadata_config_ being set,
// not on the key) and reaches encodeMetadata; sendDrainMetadata only RELEASE_ASSERTs
// metadata_config_. The soft GOAWAY fires independently of the key.
TEST_F(DrainAwareServerConnectionTest, EmptyMetadataKeyStillArmsAndEncodes) {
  bool redialed = false;
  auto conn = makeConnection([&redialed]() { redialed = true; },
                             makeMetadataConfig(std::chrono::milliseconds(2000),
                                                std::chrono::milliseconds(3000),
                                                std::chrono::milliseconds(5000), ""));
  EXPECT_CALL(*metadata_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*shutdown_notice_timer_, enableTimer(std::chrono::milliseconds(3000), _));
  conn->shutdownNotice();
  EXPECT_TRUE(redialed);

  EXPECT_CALL(*inner_ptr_, encodeMetadata(_))
      .WillOnce([](const Http::MetadataMapVector& metadata_map_vector) {
        ASSERT_EQ(1, metadata_map_vector.size());
        // The empty key is encoded verbatim.
        EXPECT_EQ("true", (*metadata_map_vector[0])[""]);
      });
  metadata_timer_->invokeCallback();

  EXPECT_CALL(*inner_ptr_, shutdownNotice());
  shutdown_notice_timer_->invokeCallback();
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
