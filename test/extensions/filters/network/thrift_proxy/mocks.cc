#include "test/extensions/filters/network/thrift_proxy/mocks.h"

#include <memory>

#include "source/common/protobuf/protobuf.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {

// Provide a specialization for ProtobufWkt::Struct (for MockFilterConfigFactory)
template <>
void MessageUtil::validate(const ProtobufWkt::Struct&, ProtobufMessage::ValidationVisitor&, bool) {}

namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

MockConfig::MockConfig() = default;
MockConfig::~MockConfig() = default;

MockTransport::MockTransport() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
}
MockTransport::~MockTransport() = default;

MockProtocol::MockProtocol() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
  ON_CALL(*this, setType(_)).WillByDefault(Invoke([&](ProtocolType type) -> void {
    type_ = type;
  }));
  ON_CALL(*this, supportsUpgrade()).WillByDefault(Return(false));
}
MockProtocol::~MockProtocol() = default;

MockDecoderCallbacks::MockDecoderCallbacks() = default;
MockDecoderCallbacks::~MockDecoderCallbacks() = default;

MockDecoderEventHandler::MockDecoderEventHandler() = default;
MockDecoderEventHandler::~MockDecoderEventHandler() = default;

MockDirectResponse::MockDirectResponse() = default;
MockDirectResponse::~MockDirectResponse() = default;

MockThriftObject::MockThriftObject() = default;
MockThriftObject::~MockThriftObject() = default;

namespace ThriftFilters {

MockFilterChainFactoryCallbacks::MockFilterChainFactoryCallbacks() = default;
MockFilterChainFactoryCallbacks::~MockFilterChainFactoryCallbacks() = default;

MockDecoderFilter::MockDecoderFilter() {
  ON_CALL(*this, transportBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, transportEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, structBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, structEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, fieldBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, fieldEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, boolValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, byteValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int16Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int32Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int64Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, doubleValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, stringValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, mapBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, mapEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, listBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, listEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, setBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, setEnd()).WillByDefault(Return(FilterStatus::Continue));
}
MockDecoderFilter::~MockDecoderFilter() = default;

MockDecoderFilterCallbacks::MockDecoderFilterCallbacks() {
  route_ = std::make_shared<NiceMock<Router::MockRoute>>();

  ON_CALL(*this, streamId()).WillByDefault(Return(stream_id_));
  ON_CALL(*this, connection()).WillByDefault(Return(&connection_));
  ON_CALL(*this, route()).WillByDefault(Return(route_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
}
MockDecoderFilterCallbacks::~MockDecoderFilterCallbacks() = default;

MockEncoderFilter::MockEncoderFilter() {
  ON_CALL(*this, transportBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, transportEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, structBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, structEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, fieldBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, fieldEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, boolValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, byteValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int16Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int32Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int64Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, doubleValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, stringValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, mapBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, mapEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, listBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, listEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, setBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, setEnd()).WillByDefault(Return(FilterStatus::Continue));
}
MockEncoderFilter::~MockEncoderFilter() = default;

MockEncoderFilterCallbacks::MockEncoderFilterCallbacks() {
  route_ = std::make_shared<NiceMock<Router::MockRoute>>();

  ON_CALL(*this, streamId()).WillByDefault(Return(stream_id_));
  ON_CALL(*this, connection()).WillByDefault(Return(&connection_));
  ON_CALL(*this, route()).WillByDefault(Return(route_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
}
MockEncoderFilterCallbacks::~MockEncoderFilterCallbacks() = default;

MockBidirectionalFilter::MockBidirectionalFilter() {
  ON_CALL(*this, decodeTransportBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeTransportEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeMessageBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeMessageEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeStructBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeStructEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeFieldBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeFieldEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeBoolValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeByteValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeInt16Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeInt32Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeInt64Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeDoubleValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeStringValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeMapBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeMapEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeListBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeListEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeSetBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, decodeSetEnd()).WillByDefault(Return(FilterStatus::Continue));

  ON_CALL(*this, encodeTransportBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeTransportEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeMessageBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeMessageEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeStructBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeStructEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeFieldBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeFieldEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeBoolValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeByteValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeInt16Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeInt32Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeInt64Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeDoubleValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeStringValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeMapBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeMapEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeListBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeListEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeSetBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, encodeSetEnd()).WillByDefault(Return(FilterStatus::Continue));
}
MockBidirectionalFilter::~MockBidirectionalFilter() = default;

// MockDecoderFilterConfigFactory
MockDecoderFilterConfigFactory::MockDecoderFilterConfigFactory()
    : name_("envoy.filters.thrift.mock_decoder_filter") {
  mock_filter_ = std::make_shared<NiceMock<MockDecoderFilter>>();
}

MockDecoderFilterConfigFactory::~MockDecoderFilterConfigFactory() = default;

FilterFactoryCb MockDecoderFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(context);

  config_struct_ = dynamic_cast<const ProtobufWkt::Struct&>(proto_config);
  config_stat_prefix_ = stats_prefix;

  return [this](FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(mock_filter_);
  };
}

MockEncoderFilterConfigFactory::MockEncoderFilterConfigFactory()
    : name_("envoy.filters.thrift.mock_encoder_filter") {
  mock_filter_ = std::make_shared<NiceMock<MockEncoderFilter>>();
}

MockEncoderFilterConfigFactory::~MockEncoderFilterConfigFactory() = default;

FilterFactoryCb MockEncoderFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(context);

  config_struct_ = dynamic_cast<const ProtobufWkt::Struct&>(proto_config);
  config_stat_prefix_ = stats_prefix;

  return [this](FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addEncoderFilter(mock_filter_);
  };
}

MockBidirectionalFilterConfigFactory::MockBidirectionalFilterConfigFactory()
    : name_("envoy.filters.thrift.mock_bidirectional_filter") {
  mock_filter_ = std::make_shared<NiceMock<MockBidirectionalFilter>>();
}

MockBidirectionalFilterConfigFactory::~MockBidirectionalFilterConfigFactory() = default;

FilterFactoryCb MockBidirectionalFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(context);

  config_struct_ = dynamic_cast<const ProtobufWkt::Struct&>(proto_config);
  config_stat_prefix_ = stats_prefix;

  return [this](FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addBidirectionalFilter(mock_filter_);
  };
}

} // namespace ThriftFilters

namespace Router {

MockRateLimitPolicyEntry::MockRateLimitPolicyEntry() {
  ON_CALL(*this, disableKey()).WillByDefault(ReturnRef(disable_key_));
}
MockRateLimitPolicyEntry::~MockRateLimitPolicyEntry() = default;

MockRateLimitPolicy::MockRateLimitPolicy() {
  ON_CALL(*this, empty()).WillByDefault(Return(true));
  ON_CALL(*this, getApplicableRateLimit(_)).WillByDefault(ReturnRef(rate_limit_policy_entry_));
}
MockRateLimitPolicy::~MockRateLimitPolicy() = default;

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, rateLimitPolicy()).WillByDefault(ReturnRef(rate_limit_policy_));
  ON_CALL(*this, clusterHeader()).WillByDefault(ReturnRef(cluster_header_));
  ON_CALL(*this, requestMirrorPolicies()).WillByDefault(ReturnRef(policies_));
}
MockRouteEntry::~MockRouteEntry() = default;

MockRoute::MockRoute() { ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_)); }
MockRoute::~MockRoute() = default;

MockShadowWriter::MockShadowWriter() {
  ON_CALL(*this, submit(_, _, _, _)).WillByDefault(Return(router_handle_));
}
MockShadowWriter::~MockShadowWriter() = default;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
