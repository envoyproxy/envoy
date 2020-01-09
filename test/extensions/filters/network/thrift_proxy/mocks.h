#pragma once

#include "envoy/router/router.h"

#include "extensions/filters/network/thrift_proxy/conn_manager.h"
#include "extensions/filters/network/thrift_proxy/conn_state.h"
#include "extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/metadata.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/router/router.h"
#include "extensions/filters/network/thrift_proxy/router/router_ratelimit.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
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
  MOCK_METHOD2(decodeFrameStart, bool(Buffer::Instance&, MessageMetadata&));
  MOCK_METHOD(bool, decodeFrameEnd, (Buffer::Instance&));
  MOCK_METHOD3(encodeFrame, void(Buffer::Instance&, const MessageMetadata&, Buffer::Instance&));

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
  MOCK_METHOD2(readMessageBegin, bool(Buffer::Instance& buffer, MessageMetadata& metadata));
  MOCK_METHOD(bool, readMessageEnd, (Buffer::Instance & buffer));
  MOCK_METHOD2(readStructBegin, bool(Buffer::Instance& buffer, std::string& name));
  MOCK_METHOD(bool, readStructEnd, (Buffer::Instance & buffer));
  MOCK_METHOD4(readFieldBegin, bool(Buffer::Instance& buffer, std::string& name,
                                    FieldType& field_type, int16_t& field_id));
  MOCK_METHOD(bool, readFieldEnd, (Buffer::Instance & buffer));
  MOCK_METHOD4(readMapBegin, bool(Buffer::Instance& buffer, FieldType& key_type,
                                  FieldType& value_type, uint32_t& size));
  MOCK_METHOD(bool, readMapEnd, (Buffer::Instance & buffer));
  MOCK_METHOD3(readListBegin, bool(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(bool, readListEnd, (Buffer::Instance & buffer));
  MOCK_METHOD3(readSetBegin, bool(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(bool, readSetEnd, (Buffer::Instance & buffer));
  MOCK_METHOD2(readBool, bool(Buffer::Instance& buffer, bool& value));
  MOCK_METHOD2(readByte, bool(Buffer::Instance& buffer, uint8_t& value));
  MOCK_METHOD2(readInt16, bool(Buffer::Instance& buffer, int16_t& value));
  MOCK_METHOD2(readInt32, bool(Buffer::Instance& buffer, int32_t& value));
  MOCK_METHOD2(readInt64, bool(Buffer::Instance& buffer, int64_t& value));
  MOCK_METHOD2(readDouble, bool(Buffer::Instance& buffer, double& value));
  MOCK_METHOD2(readString, bool(Buffer::Instance& buffer, std::string& value));
  MOCK_METHOD2(readBinary, bool(Buffer::Instance& buffer, std::string& value));

  MOCK_METHOD2(writeMessageBegin, void(Buffer::Instance& buffer, const MessageMetadata& metadata));
  MOCK_METHOD(void, writeMessageEnd, (Buffer::Instance & buffer));
  MOCK_METHOD2(writeStructBegin, void(Buffer::Instance& buffer, const std::string& name));
  MOCK_METHOD(void, writeStructEnd, (Buffer::Instance & buffer));
  MOCK_METHOD4(writeFieldBegin, void(Buffer::Instance& buffer, const std::string& name,
                                     FieldType field_type, int16_t field_id));
  MOCK_METHOD(void, writeFieldEnd, (Buffer::Instance & buffer));
  MOCK_METHOD4(writeMapBegin, void(Buffer::Instance& buffer, FieldType key_type,
                                   FieldType value_type, uint32_t size));
  MOCK_METHOD(void, writeMapEnd, (Buffer::Instance & buffer));
  MOCK_METHOD3(writeListBegin, void(Buffer::Instance& buffer, FieldType elem_type, uint32_t size));
  MOCK_METHOD(void, writeListEnd, (Buffer::Instance & buffer));
  MOCK_METHOD3(writeSetBegin, void(Buffer::Instance& buffer, FieldType elem_type, uint32_t size));
  MOCK_METHOD(void, writeSetEnd, (Buffer::Instance & buffer));
  MOCK_METHOD2(writeBool, void(Buffer::Instance& buffer, bool value));
  MOCK_METHOD2(writeByte, void(Buffer::Instance& buffer, uint8_t value));
  MOCK_METHOD2(writeInt16, void(Buffer::Instance& buffer, int16_t value));
  MOCK_METHOD2(writeInt32, void(Buffer::Instance& buffer, int32_t value));
  MOCK_METHOD2(writeInt64, void(Buffer::Instance& buffer, int64_t value));
  MOCK_METHOD2(writeDouble, void(Buffer::Instance& buffer, double value));
  MOCK_METHOD2(writeString, void(Buffer::Instance& buffer, const std::string& value));
  MOCK_METHOD2(writeBinary, void(Buffer::Instance& buffer, const std::string& value));
  MOCK_METHOD(bool, supportsUpgrade, ());
  MOCK_METHOD(DecoderEventHandlerSharedPtr, upgradeRequestDecoder, ());
  MOCK_METHOD(DirectResponsePtr, upgradeResponse, (const DecoderEventHandler&));
  MOCK_METHOD3(attemptUpgrade,
               ThriftObjectPtr(Transport&, ThriftConnectionState&, Buffer::Instance&));
  MOCK_METHOD2(completeUpgrade, void(ThriftConnectionState&, ThriftObject&));

  std::string name_{"mock"};
  ProtocolType type_{ProtocolType::Auto};
};

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  MockDecoderCallbacks();
  ~MockDecoderCallbacks() override;

  // ThriftProxy::DecoderCallbacks
  MOCK_METHOD(DecoderEventHandler&, newDecoderEventHandler, ());
};

class MockDecoderEventHandler : public DecoderEventHandler {
public:
  MockDecoderEventHandler();
  ~MockDecoderEventHandler() override;

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, transportEnd, ());
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, messageEnd, ());
  MOCK_METHOD(FilterStatus, structBegin, (const absl::string_view name));
  MOCK_METHOD(FilterStatus, structEnd, ());
  MOCK_METHOD3(fieldBegin,
               FilterStatus(const absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD(FilterStatus, fieldEnd, ());
  MOCK_METHOD(FilterStatus, boolValue, (bool& value));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value));
  MOCK_METHOD3(mapBegin, FilterStatus(FieldType& key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, mapEnd, ());
  MOCK_METHOD2(listBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, listEnd, ());
  MOCK_METHOD2(setBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, setEnd, ());
};

class MockDirectResponse : public DirectResponse {
public:
  MockDirectResponse();
  ~MockDirectResponse() override;

  // ThriftProxy::DirectResponse
  MOCK_CONST_METHOD3(encode,
                     DirectResponse::ResponseType(MessageMetadata&, Protocol&, Buffer::Instance&));
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
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter() override;

  // ThriftProxy::ThriftFilters::DecoderFilter
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks));
  MOCK_METHOD(void, resetUpstreamConnection, ());

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, transportEnd, ());
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, messageEnd, ());
  MOCK_METHOD(FilterStatus, structBegin, (absl::string_view name));
  MOCK_METHOD(FilterStatus, structEnd, ());
  MOCK_METHOD3(fieldBegin,
               FilterStatus(absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD(FilterStatus, fieldEnd, ());
  MOCK_METHOD(FilterStatus, boolValue, (bool& value));
  MOCK_METHOD(FilterStatus, byteValue, (uint8_t & value));
  MOCK_METHOD(FilterStatus, int16Value, (int16_t & value));
  MOCK_METHOD(FilterStatus, int32Value, (int32_t & value));
  MOCK_METHOD(FilterStatus, int64Value, (int64_t & value));
  MOCK_METHOD(FilterStatus, doubleValue, (double& value));
  MOCK_METHOD(FilterStatus, stringValue, (absl::string_view value));
  MOCK_METHOD3(mapBegin, FilterStatus(FieldType& key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, mapEnd, ());
  MOCK_METHOD2(listBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, listEnd, ());
  MOCK_METHOD2(setBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD(FilterStatus, setEnd, ());
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks() override;

  // ThriftProxy::ThriftFilters::DecoderFilterCallbacks
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(TransportType, downstreamTransportType, (), (const));
  MOCK_METHOD(ProtocolType, downstreamProtocolType, (), (const));
  MOCK_METHOD2(sendLocalReply, void(const DirectResponse&, bool));
  MOCK_METHOD2(startUpstreamResponse, void(Transport&, Protocol&));
  MOCK_METHOD(ResponseStatus, upstreamData, (Buffer::Instance&));
  MOCK_METHOD(void, resetDownstreamConnection, ());
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());

  uint64_t stream_id_{1};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Router::MockRoute> route_;
};

class MockFilterConfigFactory : public ThriftFilters::FactoryBase<ProtobufWkt::Struct> {
public:
  MockFilterConfigFactory();
  ~MockFilterConfigFactory() override;

  ThriftFilters::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProtobufWkt::Struct& proto_config,
                                    const std::string& stat_prefix,
                                    Server::Configuration::FactoryContext& context) override;

  std::shared_ptr<MockDecoderFilter> mock_filter_;
  ProtobufWkt::Struct config_struct_;
  std::string config_stat_prefix_;
};

} // namespace ThriftFilters

namespace Router {

class MockRateLimitPolicyEntry : public RateLimitPolicyEntry {
public:
  MockRateLimitPolicyEntry();
  ~MockRateLimitPolicyEntry() override;

  MOCK_METHOD(uint32_t, stage, (), (const));
  MOCK_METHOD(const std::string&, disableKey, (), (const));
  MOCK_CONST_METHOD5(populateDescriptors,
                     void(const RouteEntry&, std::vector<RateLimit::Descriptor>&,
                          const std::string&, const MessageMetadata&,
                          const Network::Address::Instance&));

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

  std::string cluster_name_{"fake_cluster"};
  Http::LowerCaseString cluster_header_{""};
  NiceMock<MockRateLimitPolicy> rate_limit_policy_;
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // ThriftProxy::Router::Route
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));

  NiceMock<MockRouteEntry> route_entry_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
