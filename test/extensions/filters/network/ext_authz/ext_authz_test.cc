#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/ext_authz/ext_authz.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

class ExtAuthzFilterTest : public testing::Test {
public:
  void initialize(std::string yaml) {
    envoy::extensions::filters::network::ext_authz::v3::ExtAuthz proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);
    config_ = std::make_shared<Config>(proto_config, *stats_store_.rootScope(), context_);
    client_ = new Filters::Common::ExtAuthz::MockClient();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::ExtAuthz::ClientPtr{client_});
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    addr_ = *Network::Address::PipeInstance::create("/test/test.sock");

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  Filters::Common::ExtAuthz::ResponsePtr
  makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus status) {
    Filters::Common::ExtAuthz::ResponsePtr response =
        std::make_unique<Filters::Common::ExtAuthz::Response>();
    response->status = status;
    return response;
  }

  ~ExtAuthzFilterTest() override {
    for (const Stats::GaugeSharedPtr& gauge : stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value()) << "guage name: " << gauge->name();
    }
  }

  void expectOKWithOnData() {
    filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
        ->setRemoteAddress(addr_);
    filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
        ->setLocalAddress(addr_);
    EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
        .WillOnce(
            WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
              request_callbacks_ = &callbacks;
            })));

    EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
    // Confirm that the invocation of onNewConnection did NOT increment the active or total count!
    EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.total").value());
    EXPECT_EQ(
        0U,
        stats_store_.gauge("ext_authz.name.active", Stats::Gauge::ImportMode::Accumulate).value());
    Buffer::OwnedImpl data("hello");
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
    // Confirm that the invocation of onData does increment the active and total count!
    EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
    EXPECT_EQ(
        1U,
        stats_store_.gauge("ext_authz.name.active", Stats::Gauge::ImportMode::Accumulate).value());

    filter_callbacks_.connection_.dispatcher_.globalTimeSystem().advanceTimeWait(
        std::chrono::milliseconds(10));

    Filters::Common::ExtAuthz::Response response{};
    response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
    response.headers_to_set = Filters::Common::ExtAuthz::UnsafeHeaderVector{{"foo", "bar"}};

    auto* fields = response.dynamic_metadata.mutable_fields();
    (*fields)["foo"] = ValueUtil::stringValue("ok");
    (*fields)["bar"] = ValueUtil::numberValue(1);
    (*fields)["ext_authz_duration"] = ValueUtil::numberValue(10);

    EXPECT_CALL(filter_callbacks_.connection_.stream_info_, setDynamicMetadata(_, _))
        .WillOnce(Invoke([&response](const std::string& ns,
                                     const ProtobufWkt::Struct& returned_dynamic_metadata) {
          EXPECT_EQ(ns, NetworkFilterNames::get().ExtAuthorization);
          EXPECT_TRUE(
              returned_dynamic_metadata.fields().at("ext_authz_duration").has_number_value());
          EXPECT_TRUE(
              TestUtility::protoEqual(returned_dynamic_metadata, response.dynamic_metadata));
          EXPECT_EQ(response.dynamic_metadata.fields().at("ext_authz_duration").number_value(),
                    returned_dynamic_metadata.fields().at("ext_authz_duration").number_value());
        }));

    EXPECT_CALL(filter_callbacks_, continueReading());
    request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

    EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

    EXPECT_CALL(*client_, cancel()).Times(0);
    filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);

    EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
    EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
    EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.error").value());
    EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
    EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
    EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.ok").value());
    EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
  }

  Stats::TestUtil::TestStore stats_store_;
  ConfigSharedPtr config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Filters::Common::ExtAuthz::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  Network::Address::InstanceConstSharedPtr addr_;
  Filters::Common::ExtAuthz::RequestCallbacks* request_callbacks_{};
  const std::string default_yaml_string_ = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: ext_authz_server

failure_mode_allow: true
stat_prefix: name
  )EOF";
  const std::string metadata_yaml_string_ = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: ext_authz_server
failure_mode_allow: true
stat_prefix: name
filter_enabled_metadata:
  filter: "abc.xyz"
  path:
  - key: "k1"
  value:
    string_match:
      exact: "check"
  )EOF";
};

TEST_F(ExtAuthzFilterTest, BadExtAuthzConfig) {
  std::string yaml_string = R"EOF(
grpc_service: {}
stat_prefix: name
  )EOF";

  envoy::extensions::filters::network::ext_authz::v3::ExtAuthz proto_config{};
  TestUtility::loadFromYaml(yaml_string, proto_config);

  EXPECT_THROW(
      TestUtility::downcastAndValidate<
          const envoy::extensions::filters::network::ext_authz::v3::ExtAuthz&>(proto_config),
      ProtoValidationException);
}

TEST_F(ExtAuthzFilterTest, OKWithOnData) {
  initialize(default_yaml_string_);
  expectOKWithOnData();
}

TEST_F(ExtAuthzFilterTest, DeniedWithOnData) {
  initialize(default_yaml_string_);
  InSequence s;

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  // Confirm that the invocation of onNewConnection did NOT increment the active or total count!
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(
      0U,
      stats_store_.gauge("ext_authz.name.active", Stats::Gauge::ImportMode::Accumulate).value());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  // Confirm that the invocation of onData does increment the active and total count!
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(
      1U,
      stats_store_.gauge("ext_authz.name.active", Stats::Gauge::ImportMode::Accumulate).value());

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(
      filter_callbacks_.connection_.stream_info_,
      setResponseCodeDetails(Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzDenied));
  EXPECT_CALL(*client_, cancel()).Times(0);
  request_callbacks_->onComplete(makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus::Denied));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

TEST_F(ExtAuthzFilterTest, FailOpen) {
  initialize(default_yaml_string_);
  InSequence s;

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(*client_, cancel()).Times(0);
  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->onComplete(makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus::Error));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

TEST_F(ExtAuthzFilterTest, FailClose) {
  initialize(default_yaml_string_);
  InSequence s;
  // Explicitly set the failure_mode_allow to false.
  config_->setFailModeAllow(false);

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_.connection_, close(_, _));
  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(
      filter_callbacks_.connection_.stream_info_,
      setResponseCodeDetails(Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzError));
  request_callbacks_->onComplete(makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus::Error));

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

// Test to verify that when callback from the authorization service has completed the filter
// does not invoke Cancel on RemoteClose event.
TEST_F(ExtAuthzFilterTest, DoNotCallCancelonRemoteClose) {
  initialize(default_yaml_string_);
  InSequence s;

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->onComplete(makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus::Error));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

// Test to verify that Cancel is invoked when a RemoteClose event occurs while the call
// to the authorization service was in progress.
TEST_F(ExtAuthzFilterTest, VerifyCancelOnRemoteClose) {
  initialize(default_yaml_string_);
  InSequence s;

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

// Test to verify that on stack response from the authorization service does NOT
// result in calling cancel.
TEST_F(ExtAuthzFilterTest, ImmediateOK) {
  initialize(default_yaml_string_);
  InSequence s;

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  filter_callbacks_.connection_.dispatcher_.globalTimeSystem().advanceTimeWait(
      std::chrono::milliseconds(5));
  ProtobufWkt::Struct dynamic_metadata;
  (*dynamic_metadata.mutable_fields())["baz"] = ValueUtil::stringValue("hello-ok");
  (*dynamic_metadata.mutable_fields())["x"] = ValueUtil::numberValue(12);
  // Since this is a stack response, duration should be 0;
  (*dynamic_metadata.mutable_fields())["ext_authz_duration"] = ValueUtil::numberValue(0);
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.dynamic_metadata = dynamic_metadata;

  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));

  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&dynamic_metadata](const std::string& ns,
                                           const ProtobufWkt::Struct& returned_dynamic_metadata) {
        EXPECT_TRUE(returned_dynamic_metadata.fields().contains("ext_authz_duration"));
        EXPECT_TRUE(dynamic_metadata.fields().contains("ext_authz_duration"));
        EXPECT_EQ(ns, NetworkFilterNames::get().ExtAuthorization);

        EXPECT_TRUE(TestUtility::protoEqual(returned_dynamic_metadata, dynamic_metadata));
        EXPECT_EQ(dynamic_metadata.fields().at("ext_authz_duration").number_value(),
                  returned_dynamic_metadata.fields().at("ext_authz_duration").number_value());
      }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

// Test to verify that on stack denied response from the authorization service does
// result in stoppage of the filter chain.
TEST_F(ExtAuthzFilterTest, ImmediateNOK) {
  initialize(default_yaml_string_);
  InSequence s;

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  ProtobufWkt::Struct dynamic_metadata;
  (*dynamic_metadata.mutable_fields())["baz"] = ValueUtil::stringValue("hello-nok");
  (*dynamic_metadata.mutable_fields())["x"] = ValueUtil::numberValue(15);
  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            Filters::Common::ExtAuthz::Response response{};
            response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
            response.dynamic_metadata = dynamic_metadata;
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&dynamic_metadata](const std::string& ns,
                                           const ProtobufWkt::Struct& returned_dynamic_metadata) {
        EXPECT_EQ(ns, NetworkFilterNames::get().ExtAuthorization);
        EXPECT_FALSE(returned_dynamic_metadata.fields().contains("ext_authz_duration"));
        EXPECT_FALSE(dynamic_metadata.fields().contains("ext_authz_duration"));
        EXPECT_TRUE(TestUtility::protoEqual(returned_dynamic_metadata, dynamic_metadata));
      }));
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(
      filter_callbacks_.connection_.stream_info_,
      setResponseCodeDetails(Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzDenied));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

// Test to verify that on stack Error response when failure_mode_allow is configured
// result in request being allowed.
TEST_F(ExtAuthzFilterTest, ImmediateErrorFailOpen) {
  initialize(default_yaml_string_);
  InSequence s;

  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr_);
  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus::Error));
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

// Test to verify the filter is disabled with metadata.
TEST_F(ExtAuthzFilterTest, DisabledWithMetadata) {
  initialize(metadata_yaml_string_);

  // Disable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: skip
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(filter_callbacks_.connection_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(*client_, cancel()).Times(0);

  EXPECT_EQ(1U, stats_store_.counter("ext_authz.name.disabled").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.total").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.failure_mode_allowed").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.denied").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.ok").value());
  EXPECT_EQ(0U, stats_store_.counter("ext_authz.name.cx_closed").value());
}

// Test to verify the filter is enabled with metadata.
TEST_F(ExtAuthzFilterTest, EnabledWithMetadata) {
  initialize(metadata_yaml_string_);

  // Enable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: check
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(filter_callbacks_.connection_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  expectOKWithOnData();
}

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
