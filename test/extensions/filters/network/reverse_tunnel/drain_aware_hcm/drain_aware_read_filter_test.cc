#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_read_filter.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

class DrainAwareReadFilterTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  std::unique_ptr<DrainAwareReadFilter> makeFilter(bool reverse_tunnel = true) {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    if (reverse_tunnel) {
      tunnel_iohandle_ =
          std::make_unique<Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle>(
              std::make_unique<NiceMock<Network::MockConnectionSocket>>(), nullptr, "tunnel", 1);
      ON_CALL(*socket, ioHandle()).WillByDefault(ReturnRef(*tunnel_iohandle_));
      timer_ = new NiceMock<Event::MockTimer>(&callbacks_.connection_.dispatcher_);
    }
    socket_ = std::move(socket);
    ON_CALL(callbacks_.connection_, getSocket()).WillByDefault(ReturnRef(socket_));
    auto filter = std::make_unique<DrainAwareReadFilter>(context_, drain_close_counter_);
    EXPECT_CALL(callbacks_, connection()).WillOnce(ReturnRef(callbacks_.connection_));
    filter->initializeReadFilterCallbacks(callbacks_);
    return filter;
  }

  void drain(Server::DrainStrategy strategy = Server::DrainStrategy::Immediate) {
    callbacks_.connection_.raiseConnectionDrain({simTime().monotonicTime(), strategy});
  }

  StrictMock<Network::MockReadFilterCallbacks> callbacks_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::Counter& drain_close_counter_{context_.store_.counterFromString("drain_close")};
  std::unique_ptr<Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle>
      tunnel_iohandle_;
  Network::ConnectionSocketPtr socket_;
  Event::MockTimer* timer_{nullptr};
};

TEST_F(DrainAwareReadFilterTest, DestroyBeforeInitialization) {
  DrainAwareReadFilter filter(context_, drain_close_counter_);
}

TEST_F(DrainAwareReadFilterTest, IdleTimerStartsOnlyWhenDrainArrives) {
  auto filter = makeFilter();
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  simTime().advanceTimeWait(std::chrono::seconds(60));
  EXPECT_FALSE(timer_->enabled());
  testing::Mock::VerifyAndClearExpectations(timer_);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  drain();
  EXPECT_EQ(Network::Connection::State::Open, callbacks_.connection_.state());
  testing::Mock::VerifyAndClearExpectations(&callbacks_.connection_);
  EXPECT_CALL(callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
  EXPECT_EQ(Network::Connection::State::Closed, callbacks_.connection_.state());
  EXPECT_EQ(1, drain_close_counter_.value());
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
  timer_->callback_();
  EXPECT_EQ(1, drain_close_counter_.value());
}

TEST_F(DrainAwareReadFilterTest, FirstRequestDuringPendingDrainReachesHttpCodec) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain();

  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, disableTimer());
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
  EXPECT_FALSE(timer_->enabled());
  // A callback queued before the HTTP handoff cannot close the newly active tunnel.
  timer_->callback_();
  drain();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_CALL(*timer_, disableTimer());
  filter.reset();
  EXPECT_EQ(0, drain_close_counter_.value());
}

TEST_F(DrainAwareReadFilterTest, StoppedListenerLeavesIdleTunnelToHcm) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  tunnel_iohandle_->onListenerStopped();
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  drain();
  EXPECT_FALSE(timer_->enabled());
  EXPECT_EQ(Network::Connection::State::Open, callbacks_.connection_.state());
  EXPECT_EQ(0, drain_close_counter_.value());
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
  EXPECT_EQ(0, drain_close_counter_.value());
}

TEST_F(DrainAwareReadFilterTest, ListenerStoppedAfterDrainKeepsScheduledClose) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain();
  tunnel_iohandle_->onListenerStopped();
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
  EXPECT_EQ(Network::Connection::State::Closed, callbacks_.connection_.state());
  EXPECT_EQ(1, drain_close_counter_.value());
}

TEST_F(DrainAwareReadFilterTest, FirstRequestDuringGradualDrainDoesNotRedrawOrClose) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(10)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(9000));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(2334), _));
  drain(Server::DrainStrategy::Gradual);
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
  EXPECT_FALSE(timer_->enabled());
  timer_->callback_();
  EXPECT_EQ(0, drain_close_counter_.value());
}

TEST_F(DrainAwareReadFilterTest, DestructionCancelsPendingDrain) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  drain();
  bool timer_destroyed = false;
  timer_->timer_destroyed_ = &timer_destroyed;
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(callbacks_.connection_, removeConnectionCallbacks(testing::Ref(*filter)));
  filter.reset();
  EXPECT_TRUE(timer_destroyed);
  drain();
  EXPECT_EQ(Network::Connection::State::Open, callbacks_.connection_.state());
}

TEST_F(DrainAwareReadFilterTest, ReplayedDrainWaitsForFilterInitialization) {
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_))
      .WillOnce([this](Network::ConnectionCallbacks& observer) {
        callbacks_.connection_.callbacks_.push_back(&observer);
        observer.onDrain({simTime().monotonicTime(), Server::DrainStrategy::Immediate});
      });
  auto filter = makeFilter();
  EXPECT_FALSE(timer_->enabled());
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_TRUE(timer_->enabled());
}

TEST_F(DrainAwareReadFilterTest, HttpCodecTakesOverBeforeDrain) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  Buffer::OwnedImpl data("request");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  drain();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
}

TEST_F(DrainAwareReadFilterTest, GradualDrainSamplesOnceAndRetainsOriginalDeadline) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(10)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(6000));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(6000), _));
  drain(Server::DrainStrategy::Gradual);
  simTime().advanceTimeWait(std::chrono::seconds(5));
  drain(Server::DrainStrategy::Gradual);
  drain(Server::DrainStrategy::Immediate);
  EXPECT_EQ(Network::Connection::State::Open, callbacks_.connection_.state());
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
  EXPECT_EQ(1, drain_close_counter_.value());
}

TEST_F(DrainAwareReadFilterTest, DelayedNotificationUsesOriginalStartTime) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(10)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  const Network::ConnectionDrainEvent event{simTime().monotonicTime(),
                                            Server::DrainStrategy::Gradual};
  simTime().advanceTimeWait(std::chrono::seconds(3));
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(6000));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(3000), _));
  filter->onDrain(event);
}

TEST_F(DrainAwareReadFilterTest, NotificationAfterSampledCloseTimeDrainsImmediately) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(10)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  const Network::ConnectionDrainEvent event{simTime().monotonicTime(),
                                            Server::DrainStrategy::Gradual};
  simTime().advanceTimeWait(std::chrono::seconds(3));
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(1000));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  filter->onDrain(event);
}

TEST_F(DrainAwareReadFilterTest, ZeroDrainTimeDoesNotSample) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(0)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain(Server::DrainStrategy::Gradual);
}

TEST_F(DrainAwareReadFilterTest, OneSecondGradualDrainUsesMillisecondsBeforeScaling) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(999));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(333), _));
  drain(Server::DrainStrategy::Gradual);
}

TEST_F(DrainAwareReadFilterTest, GradualDrainExcludesHcmProactiveWindowBoundary) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(666));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain(Server::DrainStrategy::Gradual);
}

TEST_F(DrainAwareReadFilterTest, NonReverseSocketIsUnchanged) {
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_)).Times(0);
  EXPECT_CALL(callbacks_.connection_.dispatcher_, createTimer_(_)).Times(0);
  auto filter = makeFilter(false);
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  drain();
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
  EXPECT_EQ(0, drain_close_counter_.value());
}

TEST_F(DrainAwareReadFilterTest, ConnectionWithoutSocketIsUnchanged) {
  ON_CALL(callbacks_.connection_, getSocket()).WillByDefault(ReturnRef(socket_));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_)).Times(0);
  EXPECT_CALL(callbacks_.connection_.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(callbacks_, connection()).WillOnce(ReturnRef(callbacks_.connection_));
  DrainAwareReadFilter filter(context_, drain_close_counter_);
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(Network::FilterStatus::Continue, filter.onNewConnection());
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::Continue, filter.onData(data, false));
  EXPECT_EQ("first request", data.toString());
  EXPECT_EQ(0, drain_close_counter_.value());
}

class DrainAwareReadFilterHealthCheckTest : public DrainAwareReadFilterTest,
                                            public testing::WithParamInterface<bool> {
protected:
  DrainAwareReadFilterHealthCheckTest() {
    runtime_.mergeValues(
        {{"envoy.reloadable_features.use_connection_event_drain", GetParam() ? "true" : "false"}});
  }
  TestScopedRuntime runtime_;
};

INSTANTIATE_TEST_SUITE_P(DrainModes, DrainAwareReadFilterHealthCheckTest, testing::Bool());

TEST_P(DrainAwareReadFilterHealthCheckTest, HealthCheckFailureAndRecoveryKeepIdleTunnelOpen) {
  auto filter = makeFilter();
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(true));
  simTime().advanceTimeWait(std::chrono::seconds(60));
  ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(false));
  EXPECT_FALSE(timer_->enabled());
}

TEST_P(DrainAwareReadFilterHealthCheckTest, HealthCheckFailureAllowsNewTunnelAndHttpCodecHandoff) {
  ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(true));
  auto filter = makeFilter();
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
}

TEST_P(DrainAwareReadFilterHealthCheckTest, FailedDefaultListenerDrainsImmediatelyOnGradualEvent) {
  ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(true));
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(10)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain(Server::DrainStrategy::Gradual);
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
}

TEST_P(DrainAwareReadFilterHealthCheckTest, FailedModifyOnlyListenerKeepsGradualDeadline) {
  auto listener_info = std::make_shared<NiceMock<Network::MockListenerInfo>>();
  ON_CALL(*listener_info, drainType())
      .WillByDefault(Return(envoy::config::listener::v3::Listener::MODIFY_ONLY));
  callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setListenerInfo(
      listener_info);
  ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(true));
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(10)));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(6000));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(6000), _));
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  drain(Server::DrainStrategy::Gradual);
  simTime().advanceTimeWait(std::chrono::seconds(5));
  EXPECT_EQ(Network::Connection::State::Open, callbacks_.connection_.state());
  testing::Mock::VerifyAndClearExpectations(&callbacks_.connection_);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
}

TEST_F(DrainAwareReadFilterTest, CloseCancelsTimerAndDestructionRemovesObserver) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  filter->onEvent(Network::ConnectionEvent::Connected);
  filter->onAboveWriteBufferHighWatermark();
  filter->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_TRUE(timer_->enabled());
  callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_FALSE(timer_->enabled());
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  drain();
  timer_->callback_();
  EXPECT_CALL(callbacks_.connection_, removeConnectionCallbacks(testing::Ref(*filter)));
  filter.reset();
  drain();
  EXPECT_EQ(0, drain_close_counter_.value());
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
