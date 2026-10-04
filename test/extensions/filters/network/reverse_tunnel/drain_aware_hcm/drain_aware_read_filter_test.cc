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
  std::unique_ptr<DrainAwareReadFilter>
  makeFilter(Network::DrainDirection direction = Network::DrainDirection::All) {
    auto filter = std::make_unique<DrainAwareReadFilter>(drain_decision_, context_, direction);
    timer_ = new NiceMock<Event::MockTimer>(&callbacks_.connection_.dispatcher_);
    EXPECT_CALL(callbacks_, connection()).WillOnce(ReturnRef(callbacks_.connection_));
    ON_CALL(callbacks_.connection_, getSocket()).WillByDefault(ReturnRef(socket_));
    filter->initializeReadFilterCallbacks(callbacks_);
    return filter;
  }

  void drain(Server::DrainStrategy strategy = Server::DrainStrategy::Immediate) {
    callbacks_.connection_.raiseConnectionDrain({simTime().monotonicTime(), strategy});
  }

  StrictMock<Network::MockReadFilterCallbacks> callbacks_;
  Network::ConnectionSocketPtr socket_;
  StrictMock<Network::MockDrainDecision> drain_decision_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Event::MockTimer* timer_{nullptr};
};

TEST_F(DrainAwareReadFilterTest, DestroyBeforeInitialization) {
  DrainAwareReadFilter filter(drain_decision_, context_, Network::DrainDirection::All);
}

TEST_F(DrainAwareReadFilterTest, IdleDrainClosesOnNextDispatcherTurnWithoutHttp2Frames) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  callbacks_.connection_.local_close_reason_.clear();
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain();
  EXPECT_EQ(Network::Connection::State::Open, callbacks_.connection_.state());
  EXPECT_TRUE(timer_->enabled());
  testing::Mock::VerifyAndClearExpectations(&callbacks_.connection_);

  EXPECT_CALL(callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_CALL(callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
  EXPECT_EQ(Network::Connection::State::Closed, callbacks_.connection_.state());
  EXPECT_EQ("reverse_tunnel_listener_draining", callbacks_.connection_.localCloseReason());
  drain();
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
}

TEST_F(DrainAwareReadFilterTest, FirstRequestDuringPendingDrainDoesNotReachHttpCodec) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain();
  EXPECT_EQ(Network::Connection::State::Open, callbacks_.connection_.state());
  testing::Mock::VerifyAndClearExpectations(&callbacks_.connection_);

  EXPECT_CALL(callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  Buffer::OwnedImpl data("first request");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(data, false));
  EXPECT_EQ("first request", data.toString());
  EXPECT_EQ(Network::Connection::State::Closed, callbacks_.connection_.state());
  EXPECT_FALSE(timer_->enabled());
  // A callback already queued before close is harmless and must not close a second time.
  timer_->callback_();
}

TEST_F(DrainAwareReadFilterTest, DestructionCancelsPendingDrain) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  drain();
  EXPECT_TRUE(timer_->enabled());

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
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  auto filter = makeFilter();
  EXPECT_FALSE(timer_->enabled());
  testing::Mock::VerifyAndClearExpectations(&callbacks_.connection_);
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onNewConnection());
}

TEST_F(DrainAwareReadFilterTest, HttpCodecTakesOverAfterFirstData) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  Buffer::OwnedImpl data("request");
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_EQ("request", data.toString());
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _)).Times(0);
  drain();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data, false));
  EXPECT_CALL(*timer_, disableTimer());
  filter.reset();
}

TEST_F(DrainAwareReadFilterTest, GradualDrainRetainsOriginalDeadline) {
  ON_CALL(context_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(10)));
  ON_CALL(context_.api_.random_, random()).WillByDefault(Return(9));
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  drain(Server::DrainStrategy::Gradual);
  simTime().advanceTimeWait(std::chrono::seconds(9));
  drain();
  timer_->invokeCallback();
  testing::Mock::VerifyAndClearExpectations(&callbacks_.connection_);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
}

TEST_F(DrainAwareReadFilterTest, HealthCheckFailureClosesIdleDefaultListener) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(true));
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
}

TEST_F(DrainAwareReadFilterTest, HealthCheckFailureDoesNotDrainModifyOnlyListener) {
  auto listener_info = std::make_shared<NiceMock<Network::MockListenerInfo>>();
  ON_CALL(*listener_info, drainType())
      .WillByDefault(Return(envoy::config::listener::v3::Listener::MODIFY_ONLY));
  callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setListenerInfo(
      listener_info);
  ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(true));
  auto filter = makeFilter();
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  timer_->invokeCallback();
}

TEST_F(DrainAwareReadFilterTest, LegacyDrainUsesInboundScopeAndRejectsFirstRequest) {
  TestScopedRuntime runtime;
  runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  auto filter = makeFilter(Network::DrainDirection::InboundOnly);
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::InboundOnly))
      .WillOnce(Return(false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::InboundOnly))
      .WillOnce(Return(true));
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  Buffer::OwnedImpl data("request");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(data, false));
}

TEST_F(DrainAwareReadFilterTest, LegacyDrainPollsAllScope) {
  TestScopedRuntime runtime;
  runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  auto filter = makeFilter();
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::All)).WillOnce(Return(false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _)).Times(0);
  drain();
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::All)).WillOnce(Return(true));
  EXPECT_CALL(callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining"));
  timer_->invokeCallback();
}

TEST_F(DrainAwareReadFilterTest, CloseStopsPollingAndDestructionRemovesObserver) {
  auto filter = makeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  filter->onEvent(Network::ConnectionEvent::Connected);
  filter->onAboveWriteBufferHighWatermark();
  filter->onBelowWriteBufferLowWatermark();
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds::zero(), _));
  drain();
  EXPECT_TRUE(timer_->enabled());
  EXPECT_CALL(*timer_, disableTimer()).Times(2);
  callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_FALSE(timer_->enabled());
  EXPECT_CALL(callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  drain();
  timer_->callback_();
  EXPECT_CALL(callbacks_.connection_, removeConnectionCallbacks(testing::Ref(*filter)));
  filter.reset();
  drain();
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
