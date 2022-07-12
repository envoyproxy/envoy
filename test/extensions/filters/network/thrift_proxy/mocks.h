#pragma once

#include "envoy/router/router.h"

#include "source/extensions/filters/network/thrift_proxy/conn_manager.h"
#include "source/extensions/filters/network/thrift_proxy/conn_state.h"
#include "source/extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/protocol.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_ratelimit.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"
#include "source/extensions/filters/network/thrift_proxy/transport.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig() override;

  // ThriftProxy::Config
  MOCK_METHOD(ThriftFilters::FilterChainFactory&, filterFactory, ());
  MOCK_METHOD(ThriftFilterStats&, stats, ());
  MOCK_METHOD(DecoderPtr, createDecoder, (DecoderCallbacks&));
  MOCK_METHOD(Router::Config&, routerConfig, ());
};

class MockTransport : public Transport {
public:
  MockTransport();
  ~MockTransport() override;

  // ThriftProxy::Transport
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(TransportType, type, (), (const));
  MOCK_METHOD(bool, decodeFrameStart, (Buffer::Instance&, MessageMetadata&));
  MOCK_METHOD(bool, decodeFrameEnd, (Buffer::Instance&));
  MOCK_METHOD(void, encodeFrame, (Buffer::Instance&, const MessageMetadata&, Buffer::Instance&));

  std::string name_{"mock"};
  TransportType type_{TransportType::Auto};
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol() override;

  // ThriftProxy::Protocol
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(ProtocolType, type, (), (const));
  MOCK_METHOD(void, setType, (ProtocolType));
  MOCK_METHOD(bool, readMessageBegin, (Buffer::Instance & buffer, MessageMetadata& metadata));
  MOCK_METHOD(bool, readMessageEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(bool, peekReplyPayload, (Buffer::Instance & buffer, ReplyType& reply_type));
  MOCK_METHOD(bool, readStructBegin, (Buffer::Instance & buffer, std::string& name));
  MOCK_METHOD(bool, readStructEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(bool, readFieldBegin,
              (Buffer::Instance & buffer, std::string& name, FieldType& field_type,
               int16_t& field_id));
  MOCK_METHOD(bool, readFieldEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(bool, readMapBegin,
              (Buffer::Instance & buffer, FieldType& key_type, FieldType& value_type,
               uint32_t& size));
  MOCK_METHOD(bool, readMapEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(bool, readListBegin,
              (Buffer::Instance & buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(bool, readListEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(bool, readSetBegin,
              (Buffer::Instance & buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(bool, readSetEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(bool, readBool, (Buffer::Instance & buffer, bool& value));
  MOCK_METHOD(bool, readByte, (Buffer::Instance & buffer, uint8_t& value));
  MOCK_METHOD(bool, readInt16, (Buffer::Instance & buffer, int16_t& value));
  MOCK_METHOD(bool, readInt32, (Buffer::Instance & buffer, int32_t& value));
  MOCK_METHOD(bool, readInt64, (Buffer::Instance & buffer, int64_t& value));
  MOCK_METHOD(bool, readDouble, (Buffer::Instance & buffer, double& value));
  MOCK_METHOD(bool, readString, (Buffer::Instance & buffer, std::string& value));
  MOCK_METHOD(bool, readBinary, (Buffer::Instance & buffer, std::string& value));

  MOCK_METHOD(void, writeMessageBegin,
              (Buffer::Instance & buffer, const MessageMetadata& metadata));
  MOCK_METHOD(void, writeMessageEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(void, writeStructBegin, (Buffer::Instance & buffer, const std::string& name));
  MOCK_METHOD(void, writeStructEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(void, writeFieldBegin,
              (Buffer::Instance & buffer, const std::string& name, FieldType field_type,
               int16_t field_id));
  MOCK_METHOD(void, writeFieldEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(void, writeMapBegin,
              (Buffer::Instance & buffer, FieldType key_type, FieldType value_type, uint32_t size));
  MOCK_METHOD(void, writeMapEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(void, writeListBegin,
              (Buffer::Instance & buffer, FieldType elem_type, uint32_t size));
  MOCK_METHOD(void, writeListEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(void, writeSetBegin, (Buffer::Instance & buffer, FieldType elem_type, uint32_t size));
  MOCK_METHOD(void, writeSetEnd, (Buffer::Instance & buffer));
  MOCK_METHOD(void, writeBool, (Buffer::Instance & buffer, bool value));
  MOCK_METHOD(void, writeByte, (Buffer::Instance & buffer, uint8_t value));
  MOCK_METHOD(void, writeInt16, (Buffer::Instance & buffer, int16_t value));
  MOCK_METHOD(void, writeInt32, (Buffer::Instance & buffer, int32_t value));
  MOCK_METHOD(void, writeInt64, (Buffer::Instance & buffer, int64_t value));
  MOCK_METHOD(void, writeDouble, (Buffer::Instance & buffer, double value));
  MOCK_METHOD(void, writeString, (Buffer::Instance & buffer, const std::string& value));
  MOCK_METHOD(void, writeBinary, (Buffer::Instance & buffer, const std::string& value));
  MOCK_METHOD(bool, supportsUpgrade, ());
  MOCK_METHOD(DecoderEventHandlerSharedPtr, upgradeRequestDecoder, ());
  MOCK_METHOD(DirectResponsePtr, upgradeResponse, (const DecoderEventHandler&));
  MOCK_METHOD(ThriftObjectPtr, attemptUpgrade,
              (Transport&, ThriftConnectionState&, Buffer::Instance&));
  MOCK_METHOD(void, completeUpgrade, (ThriftConnectionState&, ThriftObject&));

  std::string name_{"mock"};
  ProtocolType type_{ProtocolType::Auto};
};

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  MockDecoderCallbacks();
  ~MockDecoderCallbacks() override;

  // ThriftProxy::DecoderCallbacks
  MOCK_METHOD(DecoderEventHandler&, newDecoderEventHandler, ());
  MOCK_METHOD(bool, passthroughEnabled, (), (const));
  MOCK_METHOD(bool, isRequest, (), (const));
  MOCK_METHOD(bool, headerKeysPreserveCase, (), (const));
};

class MockDecoderEventHandler : public DecoderEventHandler {
public:
  MockDecoderEventHandler();
  ~MockDecoderEventHandler() override;

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, passthroughData, (Buffer::Instance & data));
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, transportEnd, ());
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, messageEnd, ());
  MOCK_METHOD(FilterStatus, structBegin, (const absl::string_view name));
  MOCK_METHOD(FilterStatus, structEnd, ());
  MOCK_METHOD(FilterStatus, fieldBegin,
              (const absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD(FilterStatus, fieldEnd, ());
  MOCK_METHOD(FilterStatus, boolValue, (bool& value));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value));
  MOCK_METHOD(FilterStatus, mapBegin,
              (FieldType & key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, mapEnd, ());
  MOCK_METHOD(FilterStatus, listBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, listEnd, ());
  MOCK_METHOD(FilterStatus, setBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, setEnd, ());
};

class MockDirectResponse : public DirectResponse {
public:
  MockDirectResponse();
  ~MockDirectResponse() override;

  // ThriftProxy::DirectResponse
  MOCK_METHOD(DirectResponse::ResponseType, encode,
              (MessageMetadata&, Protocol&, Buffer::Instance&), (const));
};

class MockThriftObject : public ThriftObject {
public:
  MockThriftObject();
  ~MockThriftObject() override;

  MOCK_METHOD(ThriftFieldPtrList&, fields, (), (const));
  MOCK_METHOD(bool, onData, (Buffer::Instance&));
};

namespace Router {
class MockRoute;
} // namespace Router

namespace ThriftFilters {

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD(void, addDecoderFilter, (DecoderFilterSharedPtr));
  MOCK_METHOD(void, addEncoderFilter, (EncoderFilterSharedPtr));
  MOCK_METHOD(void, addBidirectionalFilter, (BidirectionalFilterSharedPtr));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter() override;

  // ThriftProxy::ThriftFilters::DecoderFilter
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(ThriftFilters::LocalErrorStatus, onLocalReply,
              (const MessageMetadata& metadata, bool reset_imminent));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks));
  MOCK_METHOD(bool, passthroughSupported, (), (const));

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, passthroughData, (Buffer::Instance & data));
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, transportEnd, ());
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, messageEnd, ());
  MOCK_METHOD(FilterStatus, structBegin, (absl::string_view name));
  MOCK_METHOD(FilterStatus, structEnd, ());
  MOCK_METHOD(FilterStatus, fieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD(FilterStatus, fieldEnd, ());
  MOCK_METHOD(FilterStatus, boolValue, (bool& value));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value));
  MOCK_METHOD(FilterStatus, mapBegin,
              (FieldType & key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, mapEnd, ());
  MOCK_METHOD(FilterStatus, listBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, listEnd, ());
  MOCK_METHOD(FilterStatus, setBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, setEnd, ());
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks() override;

  // ThriftProxy::ThriftFilters::DecoderFilterCallbacks
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(TransportType, downstreamTransportType, (), (const));
  MOCK_METHOD(ProtocolType, downstreamProtocolType, (), (const));
  MOCK_METHOD(void, sendLocalReply, (const DirectResponse&, bool));
  MOCK_METHOD(void, startUpstreamResponse, (Transport&, Protocol&));
  MOCK_METHOD(ResponseStatus, upstreamData, (Buffer::Instance&));
  MOCK_METHOD(void, resetDownstreamConnection, ());
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(MessageMetadataSharedPtr, responseMetadata, ());
  MOCK_METHOD(bool, responseSuccess, ());
  MOCK_METHOD(void, onReset, ());

  uint64_t stream_id_{1};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  MessageMetadataSharedPtr metadata_;
  std::shared_ptr<Router::MockRoute> route_;
};

class MockEncoderFilter : public EncoderFilter {
public:
  MockEncoderFilter();
  ~MockEncoderFilter() override;

  // ThriftProxy::ThriftFilters::EncoderFilter
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(ThriftFilters::LocalErrorStatus, onLocalReply,
              (const MessageMetadata& metadata, bool reset_imminent));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallbacks & callbacks));
  MOCK_METHOD(bool, passthroughSupported, (), (const));

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, passthroughData, (Buffer::Instance & data));
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, transportEnd, ());
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, messageEnd, ());
  MOCK_METHOD(FilterStatus, structBegin, (absl::string_view name));
  MOCK_METHOD(FilterStatus, structEnd, ());
  MOCK_METHOD(FilterStatus, fieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD(FilterStatus, fieldEnd, ());
  MOCK_METHOD(FilterStatus, boolValue, (bool& value));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value));
  MOCK_METHOD(FilterStatus, mapBegin,
              (FieldType & key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, mapEnd, ());
  MOCK_METHOD(FilterStatus, listBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, listEnd, ());
  MOCK_METHOD(FilterStatus, setBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, setEnd, ());
};

class MockEncoderFilterCallbacks : public EncoderFilterCallbacks {
public:
  MockEncoderFilterCallbacks();
  ~MockEncoderFilterCallbacks() override;

  // ThriftProxy::ThriftFilters::EncoderFilterCallbacks
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, continueEncoding, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(TransportType, downstreamTransportType, (), (const));
  MOCK_METHOD(ProtocolType, downstreamProtocolType, (), (const));
  MOCK_METHOD(void, resetDownstreamConnection, ());
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(MessageMetadataSharedPtr, responseMetadata, ());
  MOCK_METHOD(bool, responseSuccess, ());
  MOCK_METHOD(void, onReset, ());

  uint64_t stream_id_{1};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  MessageMetadataSharedPtr metadata_;
  std::shared_ptr<Router::MockRoute> route_;
};

class MockBidirectionalFilter : public BidirectionalFilter {
public:
  MockBidirectionalFilter();
  ~MockBidirectionalFilter() override;

  // ThriftProxy::ThriftFilters::BidirectionalFilter
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(ThriftFilters::LocalErrorStatus, onLocalReply,
              (const MessageMetadata& metadata, bool reset_imminent));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallbacks & callbacks));
  MOCK_METHOD(bool, encodePassthroughSupported, (), (const));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks));
  MOCK_METHOD(bool, decodePassthroughSupported, (), (const));

  MOCK_METHOD(FilterStatus, encodePassthroughData, (Buffer::Instance & data));
  MOCK_METHOD(FilterStatus, encodeTransportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, encodeTransportEnd, ());
  MOCK_METHOD(FilterStatus, encodeMessageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, encodeMessageEnd, ());
  MOCK_METHOD(FilterStatus, encodeStructBegin, (absl::string_view name));
  MOCK_METHOD(FilterStatus, encodeStructEnd, ());
  MOCK_METHOD(FilterStatus, encodeFieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD(FilterStatus, encodeFieldEnd, ());
  MOCK_METHOD(FilterStatus, encodeBoolValue, (bool& value));
  MOCK_METHOD(FilterStatus, encodeByteValue, (uint8_t & value));
  MOCK_METHOD(FilterStatus, encodeInt16Value, (int16_t & value));
  MOCK_METHOD(FilterStatus, encodeInt32Value, (int32_t & value));
  MOCK_METHOD(FilterStatus, encodeInt64Value, (int64_t & value));
  MOCK_METHOD(FilterStatus, encodeDoubleValue, (double& value));
  MOCK_METHOD(FilterStatus, encodeStringValue, (absl::string_view value));
  MOCK_METHOD(FilterStatus, encodeMapBegin,
              (FieldType & key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, encodeMapEnd, ());
  MOCK_METHOD(FilterStatus, encodeListBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, encodeListEnd, ());
  MOCK_METHOD(FilterStatus, encodeSetBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, encodeSetEnd, ());

  MOCK_METHOD(FilterStatus, decodePassthroughData, (Buffer::Instance & data));
  MOCK_METHOD(FilterStatus, decodeTransportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, decodeTransportEnd, ());
  MOCK_METHOD(FilterStatus, decodeMessageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, decodeMessageEnd, ());
  MOCK_METHOD(FilterStatus, decodeStructBegin, (absl::string_view name));
  MOCK_METHOD(FilterStatus, decodeStructEnd, ());
  MOCK_METHOD(FilterStatus, decodeFieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD(FilterStatus, decodeFieldEnd, ());
  MOCK_METHOD(FilterStatus, decodeBoolValue, (bool& value));
  MOCK_METHOD(FilterStatus, decodeByteValue, (uint8_t & value));
  MOCK_METHOD(FilterStatus, decodeInt16Value, (int16_t & value));
  MOCK_METHOD(FilterStatus, decodeInt32Value, (int32_t & value));
  MOCK_METHOD(FilterStatus, decodeInt64Value, (int64_t & value));
  MOCK_METHOD(FilterStatus, decodeDoubleValue, (double& value));
  MOCK_METHOD(FilterStatus, decodeStringValue, (absl::string_view value));
  MOCK_METHOD(FilterStatus, decodeMapBegin,
              (FieldType & key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, decodeMapEnd, ());
  MOCK_METHOD(FilterStatus, decodeListBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, decodeListEnd, ());
  MOCK_METHOD(FilterStatus, decodeSetBegin, (FieldType & elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, decodeSetEnd, ());
};

class MockDecoderFilterConfigFactory : public NamedThriftFilterConfigFactory {
public:
  MockDecoderFilterConfigFactory();
  ~MockDecoderFilterConfigFactory() override;

  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override { return name_; }

  ProtobufWkt::Struct config_struct_;
  std::string config_stat_prefix_;

private:
  std::shared_ptr<MockDecoderFilter> mock_filter_;
  const std::string name_;
};

class MockEncoderFilterConfigFactory : public NamedThriftFilterConfigFactory {
public:
  MockEncoderFilterConfigFactory();
  ~MockEncoderFilterConfigFactory() override;

  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override { return name_; }

  ProtobufWkt::Struct config_struct_;
  std::string config_stat_prefix_;

private:
  std::shared_ptr<MockEncoderFilter> mock_filter_;
  const std::string name_;
};

class MockBidirectionalFilterConfigFactory : public NamedThriftFilterConfigFactory {
public:
  MockBidirectionalFilterConfigFactory();
  ~MockBidirectionalFilterConfigFactory() override;

  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override { return name_; }

  ProtobufWkt::Struct config_struct_;
  std::string config_stat_prefix_;

private:
  std::shared_ptr<MockBidirectionalFilter> mock_filter_;
  const std::string name_;
};

} // namespace ThriftFilters

namespace Router {

class MockRateLimitPolicyEntry : public RateLimitPolicyEntry {
public:
  MockRateLimitPolicyEntry();
  ~MockRateLimitPolicyEntry() override;

  MOCK_METHOD(uint32_t, stage, (), (const));
  MOCK_METHOD(const std::string&, disableKey, (), (const));
  MOCK_METHOD(void, populateDescriptors,
              (const RouteEntry&, std::vector<RateLimit::Descriptor>&, const std::string&,
               const MessageMetadata&, const Network::Address::Instance&),
              (const));

  std::string disable_key_;
};

class MockRateLimitPolicy : public RateLimitPolicy {
public:
  MockRateLimitPolicy();
  ~MockRateLimitPolicy() override;

  MOCK_METHOD(bool, empty, (), (const));
  MOCK_METHOD(const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&,
              getApplicableRateLimit, (uint32_t), (const));

  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limit_policy_entry_;
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // ThriftProxy::Router::RouteEntry
  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(const Envoy::Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (const));
  MOCK_METHOD(const Envoy::Router::TlsContextMatchCriteria*, tlsContextMatchCriteria, (), (const));
  MOCK_METHOD(RateLimitPolicy&, rateLimitPolicy, (), (const));
  MOCK_METHOD(bool, stripServiceName, (), (const));
  MOCK_METHOD(const Http::LowerCaseString&, clusterHeader, (), (const));
  MOCK_METHOD(const std::vector<std::shared_ptr<RequestMirrorPolicy>>&, requestMirrorPolicies, (),
              (const));

  std::string cluster_name_{"fake_cluster"};
  Http::LowerCaseString cluster_header_{""};
  NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  std::vector<std::shared_ptr<RequestMirrorPolicy>> policies_;
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // ThriftProxy::Router::Route
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));

  NiceMock<MockRouteEntry> route_entry_;
};

class MockShadowWriter : public ShadowWriter {
public:
  MockShadowWriter();
  ~MockShadowWriter() override;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, (), ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), ());
  MOCK_METHOD(absl::optional<std::reference_wrapper<ShadowRouterHandle>>, submit,
              (const std::string&, MessageMetadataSharedPtr, TransportType, ProtocolType), ());

  absl::optional<std::reference_wrapper<ShadowRouterHandle>> router_handle_{absl::nullopt};
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
