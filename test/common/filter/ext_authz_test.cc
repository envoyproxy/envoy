#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/filter/network/ext_authz.pb.validate.h"

#include "common/buffer/buffer_impl.h"
#include "common/filter/ext_authz.h"
#include "common/json/json_loader.h"
#include "common/protobuf/utility.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/ext_authz/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;
using testing::_;

namespace Envoy {
namespace ExtAuthz {
namespace TcpFilter {

class ExtAuthzFilterTest : public testing::Test {
public:
  ExtAuthzFilterTest() {
    std::string json = R"EOF(
    {
      "grpc_service": {
          "envoy_grpc": { "cluster_name": "ext_authz_server" }
      },
      "failure_mode_allow": true,
      "stat_prefix": "name"
    }
    )EOF";

    envoy::api::v2::filter::network::ExtAuthz proto_config{};
    MessageUtil::loadFromJson(json, proto_config);
    config_.reset(new Config(proto_config, stats_store_, runtime_, cm_));
    client_ = new MockClient();
    filter_.reset(new Instance(config_, ClientPtr{client_}));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    check_req_generator_ = new NiceMock<MockCheckRequestGen>();
    filter_->setCheckReqGenerator(check_req_generator_);

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  ~ExtAuthzFilterTest() {
    for (const Stats::GaugeSharedPtr& gauge : stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<MockCheckRequestGen>* check_req_generator_;
  ConfigSharedPtr config_;
  MockClient* client_;
  std::unique_ptr<Instance> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  RequestCallbacks* request_callbacks_{};
};

TEST_F(ExtAuthzFilterTest, BadExtAuthzConfig) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "grpc_service": {}
  }
  )EOF";

  envoy::api::v2::filter::network::ExtAuthz proto_config{};
  MessageUtil::loadFromJson(json_string, proto_config);

  EXPECT_THROW(MessageUtil::downcastAndValidate<const envoy::api::v2::filter::network::ExtAuthz&>(
                   proto_config),
               ProtoValidationException);
}

TEST_F(ExtAuthzFilterTest, OK) {
  InSequence s;

  EXPECT_CALL(*check_req_generator_, createTcpCheck(_, _));
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>()))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_.connection_, readDisable(false));
  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(CheckStatus::OK);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.ok").value());
}

TEST_F(ExtAuthzFilterTest, Denied) {
  InSequence s;

  EXPECT_CALL(*check_req_generator_, createTcpCheck(_, _));
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_.connection_, readDisable(false));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*client_, cancel()).Times(0);
  request_callbacks_->complete(CheckStatus::Denied);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.unauthz").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

TEST_F(ExtAuthzFilterTest, OKWithSSLConnect) {
  InSequence s;

  EXPECT_CALL(*check_req_generator_, createTcpCheck(_, _));
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>()))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  // Called by SSL when the handshake is done.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::Connected);
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_.connection_, readDisable(false));
  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(CheckStatus::OK);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.ok").value());
}

TEST_F(ExtAuthzFilterTest, DeniedWithSSLConnect) {
  InSequence s;

  EXPECT_CALL(*check_req_generator_, createTcpCheck(_, _));
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  // Called by SSL when the handshake is done.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::Connected);
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_.connection_, readDisable(false));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*client_, cancel()).Times(0);
  request_callbacks_->complete(CheckStatus::Denied);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.unauthz").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

TEST_F(ExtAuthzFilterTest, FailOpen) {
  InSequence s;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(*client_, cancel()).Times(0);
  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(CheckStatus::Error);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.unauthz").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

TEST_F(ExtAuthzFilterTest, Error) {
  InSequence s;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(CheckStatus::Error);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.error").value());
}

TEST_F(ExtAuthzFilterTest, Disconnect) {
  InSequence s;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
}

TEST_F(ExtAuthzFilterTest, ImmediateOK) {
  InSequence s;

  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(Invoke(
          [&](RequestCallbacks& callbacks) -> void { callbacks.complete(CheckStatus::OK); })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.ok").value());
}

} // namespace TcpFilter
} // namespace ExtAuthz
} // namespace Envoy
