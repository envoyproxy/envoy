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
  MOCK_METHOD(ThriftFilters::FilterChainFactory&, filterFactory, (), (override));
  MOCK_METHOD(ThriftFilterStats&, stats, (), (override));
  MOCK_METHOD(DecoderPtr, createDecoder, (DecoderCallbacks&));
  MOCK_METHOD(Router::Config&, routerConfig, (), (override));
};

class MockTransport : public Transport {
public:
  MockTransport();
  ~MockTransport() override;

  // ThriftProxy::Transport
  MOCK_METHOD(const std::string&, name, (), (const, override));
  MOCK_METHOD(TransportType, type, (), (const, override));
  MOCK_METHOD(bool, decodeFrameStart, (Buffer::Instance&, MessageMetadata&), (override));
  MOCK_METHOD(bool, decodeFrameEnd, (Buffer::Instance&), (override));
  MOCK_METHOD(void, encodeFrame, (Buffer::Instance&, const MessageMetadata&, Buffer::Instance&),
              (override));

  std::string name_{"mock"};
  TransportType type_{TransportType::Auto};
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol() override;

  // ThriftProxy::Protocol
  MOCK_METHOD(const std::string&, name, (), (const, override));
  MOCK_METHOD(ProtocolType, type, (), (const, override));
  MOCK_METHOD(void, setType, (ProtocolType), (override));
  MOCK_METHOD(bool, readMessageBegin, (Buffer::Instance & buffer, MessageMetadata& metadata),
              (override));
  MOCK_METHOD(bool, readMessageEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(bool, peekReplyPayload, (Buffer::Instance & buffer, ReplyType& reply_type),
              (override));
  MOCK_METHOD(bool, readStructBegin, (Buffer::Instance & buffer, std::string& name), (override));
  MOCK_METHOD(bool, readStructEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(bool, readFieldBegin,
              (Buffer::Instance & buffer, std::string& name, FieldType& field_type,
               int16_t& field_id),
              (override));
  MOCK_METHOD(bool, readFieldEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(bool, readMapBegin,
              (Buffer::Instance & buffer, FieldType& key_type, FieldType& value_type,
               uint32_t& size),
              (override));
  MOCK_METHOD(bool, readMapEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(bool, readListBegin,
              (Buffer::Instance & buffer, FieldType& elem_type, uint32_t& size), (override));
  MOCK_METHOD(bool, readListEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(bool, readSetBegin, (Buffer::Instance & buffer, FieldType& elem_type, uint32_t& size),
              (override));
  MOCK_METHOD(bool, readSetEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(bool, readBool, (Buffer::Instance & buffer, bool& value), (override));
  MOCK_METHOD(bool, readByte, (Buffer::Instance & buffer, uint8_t& value), (override));
  MOCK_METHOD(bool, readInt16, (Buffer::Instance & buffer, int16_t& value), (override));
  MOCK_METHOD(bool, readInt32, (Buffer::Instance & buffer, int32_t& value), (override));
  MOCK_METHOD(bool, readInt64, (Buffer::Instance & buffer, int64_t& value), (override));
  MOCK_METHOD(bool, readDouble, (Buffer::Instance & buffer, double& value), (override));
  MOCK_METHOD(bool, readString, (Buffer::Instance & buffer, std::string& value), (override));
  MOCK_METHOD(bool, readBinary, (Buffer::Instance & buffer, std::string& value), (override));

  MOCK_METHOD(void, writeMessageBegin, (Buffer::Instance & buffer, const MessageMetadata& metadata),
              (override));
  MOCK_METHOD(void, writeMessageEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(void, writeStructBegin, (Buffer::Instance & buffer, const std::string& name),
              (override));
  MOCK_METHOD(void, writeStructEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(void, writeFieldBegin,
              (Buffer::Instance & buffer, const std::string& name, FieldType field_type,
               int16_t field_id),
              (override));
  MOCK_METHOD(void, writeFieldEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(void, writeMapBegin,
              (Buffer::Instance & buffer, FieldType key_type, FieldType value_type, uint32_t size),
              (override));
  MOCK_METHOD(void, writeMapEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(void, writeListBegin, (Buffer::Instance & buffer, FieldType elem_type, uint32_t size),
              (override));
  MOCK_METHOD(void, writeListEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(void, writeSetBegin, (Buffer::Instance & buffer, FieldType elem_type, uint32_t size),
              (override));
  MOCK_METHOD(void, writeSetEnd, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(void, writeBool, (Buffer::Instance & buffer, bool value), (override));
  MOCK_METHOD(void, writeByte, (Buffer::Instance & buffer, uint8_t value), (override));
  MOCK_METHOD(void, writeInt16, (Buffer::Instance & buffer, int16_t value), (override));
  MOCK_METHOD(void, writeInt32, (Buffer::Instance & buffer, int32_t value), (override));
  MOCK_METHOD(void, writeInt64, (Buffer::Instance & buffer, int64_t value), (override));
  MOCK_METHOD(void, writeDouble, (Buffer::Instance & buffer, double value), (override));
  MOCK_METHOD(void, writeString, (Buffer::Instance & buffer, const std::string& value), (override));
  MOCK_METHOD(void, writeBinary, (Buffer::Instance & buffer, const std::string& value), (override));
  MOCK_METHOD(bool, supportsUpgrade, (), (override));
  MOCK_METHOD(DecoderEventHandlerSharedPtr, upgradeRequestDecoder, (), (override));
  MOCK_METHOD(DirectResponsePtr, upgradeResponse, (const DecoderEventHandler&), (override));
  MOCK_METHOD(ThriftObjectPtr, attemptUpgrade,
              (Transport&, ThriftConnectionState&, Buffer::Instance&), (override));
  MOCK_METHOD(void, completeUpgrade, (ThriftConnectionState&, ThriftObject&), (override));

  std::string name_{"mock"};
  ProtocolType type_{ProtocolType::Auto};
};

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  MockDecoderCallbacks();
  ~MockDecoderCallbacks() override;

  // ThriftProxy::DecoderCallbacks
  MOCK_METHOD(DecoderEventHandler&, newDecoderEventHandler, (), (override));
  MOCK_METHOD(bool, passthroughEnabled, (), (const, override));
  MOCK_METHOD(bool, isRequest, (), (const, override));
  MOCK_METHOD(bool, headerKeysPreserveCase, (), (const, override));
};

class MockDecoderEventHandler : public DecoderEventHandler {
public:
  MockDecoderEventHandler();
  ~MockDecoderEventHandler() override;

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, passthroughData, (Buffer::Instance & data), (override));
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, transportEnd, (), (override));
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, messageEnd, (), (override));
  MOCK_METHOD(FilterStatus, structBegin, (const absl::string_view name), (override));
  MOCK_METHOD(FilterStatus, structEnd, (), (override));
  MOCK_METHOD(FilterStatus, fieldBegin,
              (const absl::string_view name, FieldType& msg_type, int16_t& field_id), (override));
  MOCK_METHOD(FilterStatus, fieldEnd, (), (override));
  MOCK_METHOD(FilterStatus, boolValue, (bool& value), (override));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value), (override));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value), (override));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value), (override));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value), (override));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value), (override));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value), (override));
  MOCK_METHOD(FilterStatus, mapBegin, (FieldType & key_type, FieldType& value_type, uint32_t& size),
              (override));
  MOCK_METHOD(FilterStatus, mapEnd, (), (override));
  MOCK_METHOD(FilterStatus, listBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, listEnd, (), (override));
  MOCK_METHOD(FilterStatus, setBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, setEnd, (), (override));
};

class MockDirectResponse : public DirectResponse {
public:
  MockDirectResponse();
  ~MockDirectResponse() override;

  // ThriftProxy::DirectResponse
  MOCK_METHOD(DirectResponse::ResponseType, encode,
              (MessageMetadata&, Protocol&, Buffer::Instance&), (const, override));
};

class MockThriftObject : public ThriftObject {
public:
  MockThriftObject();
  ~MockThriftObject() override;

  MOCK_METHOD(ThriftFieldPtrList&, fields, (), (const, override));
  MOCK_METHOD(bool, onData, (Buffer::Instance&), (override));
};

namespace Router {
class MockRoute;
} // namespace Router

namespace ThriftFilters {

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD(void, addDecoderFilter, (DecoderFilterSharedPtr), (override));
  MOCK_METHOD(void, addEncoderFilter, (EncoderFilterSharedPtr), (override));
  MOCK_METHOD(void, addBidirectionalFilter, (BidirectionalFilterSharedPtr), (override));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter() override;

  // ThriftProxy::ThriftFilters::DecoderFilter
  MOCK_METHOD(void, onDestroy, (), (override));
  MOCK_METHOD(ThriftFilters::LocalErrorStatus, onLocalReply,
              (const MessageMetadata& metadata, bool reset_imminent), (override));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks), (override));
  MOCK_METHOD(bool, passthroughSupported, (), (const, override));

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, passthroughData, (Buffer::Instance & data), (override));
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, transportEnd, (), (override));
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, messageEnd, (), (override));
  MOCK_METHOD(FilterStatus, structBegin, (absl::string_view name), (override));
  MOCK_METHOD(FilterStatus, structEnd, (), (override));
  MOCK_METHOD(FilterStatus, fieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id), (override));
  MOCK_METHOD(FilterStatus, fieldEnd, (), (override));
  MOCK_METHOD(FilterStatus, boolValue, (bool& value), (override));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value), (override));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value), (override));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value), (override));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value), (override));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value), (override));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value), (override));
  MOCK_METHOD(FilterStatus, mapBegin, (FieldType & key_type, FieldType& value_type, uint32_t& size),
              (override));
  MOCK_METHOD(FilterStatus, mapEnd, (), (override));
  MOCK_METHOD(FilterStatus, listBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, listEnd, (), (override));
  MOCK_METHOD(FilterStatus, setBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, setEnd, (), (override));
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks() override;

  // ThriftProxy::ThriftFilters::DecoderFilterCallbacks
  MOCK_METHOD(uint64_t, streamId, (), (const, override));
  MOCK_METHOD(const Network::Connection*, connection, (), (const, override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(void, continueDecoding, (), (override));
  MOCK_METHOD(Router::RouteConstSharedPtr, route, (), (override));
  MOCK_METHOD(TransportType, downstreamTransportType, (), (const, override));
  MOCK_METHOD(ProtocolType, downstreamProtocolType, (), (const, override));
  MOCK_METHOD(void, sendLocalReply, (const DirectResponse&, bool), (override));
  MOCK_METHOD(void, startUpstreamResponse, (Transport&, Protocol&), (override));
  MOCK_METHOD(ResponseStatus, upstreamData, (Buffer::Instance&), (override));
  MOCK_METHOD(void, clearRouteCache, (), (override));
  MOCK_METHOD(void, resetDownstreamConnection, (), (override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(MessageMetadataSharedPtr, responseMetadata, (), (override));
  MOCK_METHOD(bool, responseSuccess, (), (override));
  MOCK_METHOD(void, onReset, (), (override));

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
  MOCK_METHOD(void, onDestroy, (), (override));
  MOCK_METHOD(ThriftFilters::LocalErrorStatus, onLocalReply,
              (const MessageMetadata& metadata, bool reset_imminent), (override));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallbacks & callbacks), (override));
  MOCK_METHOD(bool, passthroughSupported, (), (const, override));

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, passthroughData, (Buffer::Instance & data), (override));
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, transportEnd, (), (override));
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, messageEnd, (), (override));
  MOCK_METHOD(FilterStatus, structBegin, (absl::string_view name), (override));
  MOCK_METHOD(FilterStatus, structEnd, (), (override));
  MOCK_METHOD(FilterStatus, fieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id), (override));
  MOCK_METHOD(FilterStatus, fieldEnd, (), (override));
  MOCK_METHOD(FilterStatus, boolValue, (bool& value), (override));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value), (override));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value), (override));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value), (override));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value), (override));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value), (override));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value), (override));
  MOCK_METHOD(FilterStatus, mapBegin, (FieldType & key_type, FieldType& value_type, uint32_t& size),
              (override));
  MOCK_METHOD(FilterStatus, mapEnd, (), (override));
  MOCK_METHOD(FilterStatus, listBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, listEnd, (), (override));
  MOCK_METHOD(FilterStatus, setBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, setEnd, (), (override));
};

class MockEncoderFilterCallbacks : public EncoderFilterCallbacks {
public:
  MockEncoderFilterCallbacks();
  ~MockEncoderFilterCallbacks() override;

  // ThriftProxy::ThriftFilters::EncoderFilterCallbacks
  MOCK_METHOD(uint64_t, streamId, (), (const, override));
  MOCK_METHOD(const Network::Connection*, connection, (), (const, override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(void, continueEncoding, (), (override));
  MOCK_METHOD(Router::RouteConstSharedPtr, route, (), (override));
  MOCK_METHOD(TransportType, downstreamTransportType, (), (const, override));
  MOCK_METHOD(ProtocolType, downstreamProtocolType, (), (const, override));
  MOCK_METHOD(void, resetDownstreamConnection, (), (override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(MessageMetadataSharedPtr, responseMetadata, (), (override));
  MOCK_METHOD(bool, responseSuccess, (), (override));
  MOCK_METHOD(void, onReset, (), (override));

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
  MOCK_METHOD(void, onDestroy, (), (override));
  MOCK_METHOD(ThriftFilters::LocalErrorStatus, onLocalReply,
              (const MessageMetadata& metadata, bool reset_imminent), (override));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallbacks & callbacks), (override));
  MOCK_METHOD(bool, encodePassthroughSupported, (), (const, override));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks), (override));
  MOCK_METHOD(bool, decodePassthroughSupported, (), (const, override));

  MOCK_METHOD(FilterStatus, encodePassthroughData, (Buffer::Instance & data), (override));
  MOCK_METHOD(FilterStatus, encodeTransportBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, encodeTransportEnd, (), (override));
  MOCK_METHOD(FilterStatus, encodeMessageBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, encodeMessageEnd, (), (override));
  MOCK_METHOD(FilterStatus, encodeStructBegin, (absl::string_view name), (override));
  MOCK_METHOD(FilterStatus, encodeStructEnd, (), (override));
  MOCK_METHOD(FilterStatus, encodeFieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id), (override));
  MOCK_METHOD(FilterStatus, encodeFieldEnd, (), (override));
  MOCK_METHOD(FilterStatus, encodeBoolValue, (bool& value), (override));
  MOCK_METHOD(FilterStatus, encodeByteValue, (uint8_t & value), (override));
  MOCK_METHOD(FilterStatus, encodeInt16Value, (int16_t & value), (override));
  MOCK_METHOD(FilterStatus, encodeInt32Value, (int32_t & value), (override));
  MOCK_METHOD(FilterStatus, encodeInt64Value, (int64_t & value), (override));
  MOCK_METHOD(FilterStatus, encodeDoubleValue, (double& value), (override));
  MOCK_METHOD(FilterStatus, encodeStringValue, (absl::string_view value), (override));
  MOCK_METHOD(FilterStatus, encodeMapBegin,
              (FieldType & key_type, FieldType& value_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, encodeMapEnd, (), (override));
  MOCK_METHOD(FilterStatus, encodeListBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, encodeListEnd, (), (override));
  MOCK_METHOD(FilterStatus, encodeSetBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, encodeSetEnd, (), (override));

  MOCK_METHOD(FilterStatus, decodePassthroughData, (Buffer::Instance & data), (override));
  MOCK_METHOD(FilterStatus, decodeTransportBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, decodeTransportEnd, (), (override));
  MOCK_METHOD(FilterStatus, decodeMessageBegin, (MessageMetadataSharedPtr metadata), (override));
  MOCK_METHOD(FilterStatus, decodeMessageEnd, (), (override));
  MOCK_METHOD(FilterStatus, decodeStructBegin, (absl::string_view name), (override));
  MOCK_METHOD(FilterStatus, decodeStructEnd, (), (override));
  MOCK_METHOD(FilterStatus, decodeFieldBegin,
              (absl::string_view name, FieldType& msg_type, int16_t& field_id), (override));
  MOCK_METHOD(FilterStatus, decodeFieldEnd, (), (override));
  MOCK_METHOD(FilterStatus, decodeBoolValue, (bool& value), (override));
  MOCK_METHOD(FilterStatus, decodeByteValue, (uint8_t & value), (override));
  MOCK_METHOD(FilterStatus, decodeInt16Value, (int16_t & value), (override));
  MOCK_METHOD(FilterStatus, decodeInt32Value, (int32_t & value), (override));
  MOCK_METHOD(FilterStatus, decodeInt64Value, (int64_t & value), (override));
  MOCK_METHOD(FilterStatus, decodeDoubleValue, (double& value), (override));
  MOCK_METHOD(FilterStatus, decodeStringValue, (absl::string_view value), (override));
  MOCK_METHOD(FilterStatus, decodeMapBegin,
              (FieldType & key_type, FieldType& value_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, decodeMapEnd, (), (override));
  MOCK_METHOD(FilterStatus, decodeListBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, decodeListEnd, (), (override));
  MOCK_METHOD(FilterStatus, decodeSetBegin, (FieldType & elem_type, uint32_t& size), (override));
  MOCK_METHOD(FilterStatus, decodeSetEnd, (), (override));
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
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return name_; }

  Protobuf::Struct config_struct_;
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
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return name_; }

  Protobuf::Struct config_struct_;
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
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return name_; }

  Protobuf::Struct config_struct_;
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

  MOCK_METHOD(uint32_t, stage, (), (const, override));
  MOCK_METHOD(const std::string&, disableKey, (), (const, override));
  MOCK_METHOD(void, populateDescriptors,
              (const RouteEntry&, std::vector<RateLimit::Descriptor>&, const std::string&,
               const MessageMetadata&, const Network::Address::Instance&),
              (const, override));

  std::string disable_key_;
};

class MockRateLimitPolicy : public RateLimitPolicy {
public:
  MockRateLimitPolicy();
  ~MockRateLimitPolicy() override;

  MOCK_METHOD(bool, empty, (), (const, override));
  MOCK_METHOD(const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&,
              getApplicableRateLimit, (uint32_t), (const, override));

  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limit_policy_entry_;
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // ThriftProxy::Router::RouteEntry
  MOCK_METHOD(const std::string&, clusterName, (), (const, override));
  MOCK_METHOD(const Envoy::Router::MetadataMatchCriteria*, metadataMatchCriteria, (),
              (const, override));
  MOCK_METHOD(const Envoy::Router::TlsContextMatchCriteria*, tlsContextMatchCriteria, (),
              (const, override));
  MOCK_METHOD(RateLimitPolicy&, rateLimitPolicy, (), (const, override));
  MOCK_METHOD(bool, stripServiceName, (), (const, override));
  MOCK_METHOD(const Http::LowerCaseString&, clusterHeader, (), (const, override));
  MOCK_METHOD(const std::vector<std::shared_ptr<RequestMirrorPolicy>>&, requestMirrorPolicies, (),
              (const, override));

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
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const, override));

  NiceMock<MockRouteEntry> route_entry_;
};

class MockShadowWriter : public ShadowWriter {
public:
  MockShadowWriter();
  ~MockShadowWriter() override;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, (), (override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(OptRef<ShadowRouterHandle>, submit,
              (const std::string&, MessageMetadataSharedPtr, TransportType, ProtocolType),
              (override));

  OptRef<ShadowRouterHandle> router_handle_{std::nullopt};
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
