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
  MOCK_METHOD0(filterFactory, ThriftFilters::FilterChainFactory&());
  MOCK_METHOD0(stats, ThriftFilterStats&());
  MOCK_METHOD1(createDecoder, DecoderPtr(DecoderCallbacks&));
  MOCK_METHOD0(routerConfig, Router::Config&());
};

class MockTransport : public Transport {
public:
  MockTransport();
  ~MockTransport() override;

  // ThriftProxy::Transport
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(type, TransportType());
  MOCK_METHOD2(decodeFrameStart, bool(Buffer::Instance&, MessageMetadata&));
  MOCK_METHOD1(decodeFrameEnd, bool(Buffer::Instance&));
  MOCK_METHOD3(encodeFrame, void(Buffer::Instance&, const MessageMetadata&, Buffer::Instance&));

  std::string name_{"mock"};
  TransportType type_{TransportType::Auto};
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol() override;

  // ThriftProxy::Protocol
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(type, ProtocolType());
  MOCK_METHOD1(setType, void(ProtocolType));
  MOCK_METHOD2(readMessageBegin, bool(Buffer::Instance& buffer, MessageMetadata& metadata));
  MOCK_METHOD1(readMessageEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD2(readStructBegin, bool(Buffer::Instance& buffer, std::string& name));
  MOCK_METHOD1(readStructEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD4(readFieldBegin, bool(Buffer::Instance& buffer, std::string& name,
                                    FieldType& field_type, int16_t& field_id));
  MOCK_METHOD1(readFieldEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD4(readMapBegin, bool(Buffer::Instance& buffer, FieldType& key_type,
                                  FieldType& value_type, uint32_t& size));
  MOCK_METHOD1(readMapEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD3(readListBegin, bool(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD1(readListEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD3(readSetBegin, bool(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD1(readSetEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD2(readBool, bool(Buffer::Instance& buffer, bool& value));
  MOCK_METHOD2(readByte, bool(Buffer::Instance& buffer, uint8_t& value));
  MOCK_METHOD2(readInt16, bool(Buffer::Instance& buffer, int16_t& value));
  MOCK_METHOD2(readInt32, bool(Buffer::Instance& buffer, int32_t& value));
  MOCK_METHOD2(readInt64, bool(Buffer::Instance& buffer, int64_t& value));
  MOCK_METHOD2(readDouble, bool(Buffer::Instance& buffer, double& value));
  MOCK_METHOD2(readString, bool(Buffer::Instance& buffer, std::string& value));
  MOCK_METHOD2(readBinary, bool(Buffer::Instance& buffer, std::string& value));

  MOCK_METHOD2(writeMessageBegin, void(Buffer::Instance& buffer, const MessageMetadata& metadata));
  MOCK_METHOD1(writeMessageEnd, void(Buffer::Instance& buffer));
  MOCK_METHOD2(writeStructBegin, void(Buffer::Instance& buffer, const std::string& name));
  MOCK_METHOD1(writeStructEnd, void(Buffer::Instance& buffer));
  MOCK_METHOD4(writeFieldBegin, void(Buffer::Instance& buffer, const std::string& name,
                                     FieldType field_type, int16_t field_id));
  MOCK_METHOD1(writeFieldEnd, void(Buffer::Instance& buffer));
  MOCK_METHOD4(writeMapBegin, void(Buffer::Instance& buffer, FieldType key_type,
                                   FieldType value_type, uint32_t size));
  MOCK_METHOD1(writeMapEnd, void(Buffer::Instance& buffer));
  MOCK_METHOD3(writeListBegin, void(Buffer::Instance& buffer, FieldType elem_type, uint32_t size));
  MOCK_METHOD1(writeListEnd, void(Buffer::Instance& buffer));
  MOCK_METHOD3(writeSetBegin, void(Buffer::Instance& buffer, FieldType elem_type, uint32_t size));
  MOCK_METHOD1(writeSetEnd, void(Buffer::Instance& buffer));
  MOCK_METHOD2(writeBool, void(Buffer::Instance& buffer, bool value));
  MOCK_METHOD2(writeByte, void(Buffer::Instance& buffer, uint8_t value));
  MOCK_METHOD2(writeInt16, void(Buffer::Instance& buffer, int16_t value));
  MOCK_METHOD2(writeInt32, void(Buffer::Instance& buffer, int32_t value));
  MOCK_METHOD2(writeInt64, void(Buffer::Instance& buffer, int64_t value));
  MOCK_METHOD2(writeDouble, void(Buffer::Instance& buffer, double value));
  MOCK_METHOD2(writeString, void(Buffer::Instance& buffer, const std::string& value));
  MOCK_METHOD2(writeBinary, void(Buffer::Instance& buffer, const std::string& value));
  MOCK_METHOD0(supportsUpgrade, bool());
  MOCK_METHOD0(upgradeRequestDecoder, DecoderEventHandlerSharedPtr());
  MOCK_METHOD1(upgradeResponse, DirectResponsePtr(const DecoderEventHandler&));
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
  MOCK_METHOD0(newDecoderEventHandler, DecoderEventHandler&());
};

class MockDecoderEventHandler : public DecoderEventHandler {
public:
  MockDecoderEventHandler();
  ~MockDecoderEventHandler() override;

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD1(transportBegin, FilterStatus(MessageMetadataSharedPtr metadata));
  MOCK_METHOD0(transportEnd, FilterStatus());
  MOCK_METHOD1(messageBegin, FilterStatus(MessageMetadataSharedPtr metadata));
  MOCK_METHOD0(messageEnd, FilterStatus());
  MOCK_METHOD1(structBegin, FilterStatus(const absl::string_view name));
  MOCK_METHOD0(structEnd, FilterStatus());
  MOCK_METHOD3(fieldBegin,
               FilterStatus(const absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD0(fieldEnd, FilterStatus());
  MOCK_METHOD1(boolValue, FilterStatus(bool& value));
  MOCK_METHOD1(byteValue, FilterStatus(uint8_t& value));
  MOCK_METHOD1(int16Value, FilterStatus(int16_t& value));
  MOCK_METHOD1(int32Value, FilterStatus(int32_t& value));
  MOCK_METHOD1(int64Value, FilterStatus(int64_t& value));
  MOCK_METHOD1(doubleValue, FilterStatus(double& value));
  MOCK_METHOD1(stringValue, FilterStatus(absl::string_view value));
  MOCK_METHOD3(mapBegin, FilterStatus(FieldType& key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD0(mapEnd, FilterStatus());
  MOCK_METHOD2(listBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD0(listEnd, FilterStatus());
  MOCK_METHOD2(setBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD0(setEnd, FilterStatus());
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

  MOCK_CONST_METHOD0(fields, ThriftFieldPtrList&());
  MOCK_METHOD1(onData, bool(Buffer::Instance&));
};

namespace Router {
class MockRoute;
} // namespace Router

namespace ThriftFilters {

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD1(addDecoderFilter, void(DecoderFilterSharedPtr));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter() override;

  // ThriftProxy::ThriftFilters::DecoderFilter
  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD1(setDecoderFilterCallbacks, void(DecoderFilterCallbacks& callbacks));
  MOCK_METHOD0(resetUpstreamConnection, void());

  // ThriftProxy::DecoderEventHandler
  MOCK_METHOD1(transportBegin, FilterStatus(MessageMetadataSharedPtr metadata));
  MOCK_METHOD0(transportEnd, FilterStatus());
  MOCK_METHOD1(messageBegin, FilterStatus(MessageMetadataSharedPtr metadata));
  MOCK_METHOD0(messageEnd, FilterStatus());
  MOCK_METHOD1(structBegin, FilterStatus(absl::string_view name));
  MOCK_METHOD0(structEnd, FilterStatus());
  MOCK_METHOD3(fieldBegin,
               FilterStatus(absl::string_view name, FieldType& msg_type, int16_t& field_id));
  MOCK_METHOD0(fieldEnd, FilterStatus());
  MOCK_METHOD1(boolValue, FilterStatus(bool& value));
  MOCK_METHOD1(byteValue, FilterStatus(uint8_t& value));
  MOCK_METHOD1(int16Value, FilterStatus(int16_t& value));
  MOCK_METHOD1(int32Value, FilterStatus(int32_t& value));
  MOCK_METHOD1(int64Value, FilterStatus(int64_t& value));
  MOCK_METHOD1(doubleValue, FilterStatus(double& value));
  MOCK_METHOD1(stringValue, FilterStatus(absl::string_view value));
  MOCK_METHOD3(mapBegin, FilterStatus(FieldType& key_type, FieldType& value_type, uint32_t& size));
  MOCK_METHOD0(mapEnd, FilterStatus());
  MOCK_METHOD2(listBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD0(listEnd, FilterStatus());
  MOCK_METHOD2(setBegin, FilterStatus(FieldType& elem_type, uint32_t& size));
  MOCK_METHOD0(setEnd, FilterStatus());
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks() override;

  // ThriftProxy::ThriftFilters::DecoderFilterCallbacks
  MOCK_CONST_METHOD0(streamId, uint64_t());
  MOCK_CONST_METHOD0(connection, const Network::Connection*());
  MOCK_METHOD0(continueDecoding, void());
  MOCK_METHOD0(route, Router::RouteConstSharedPtr());
  MOCK_CONST_METHOD0(downstreamTransportType, TransportType());
  MOCK_CONST_METHOD0(downstreamProtocolType, ProtocolType());
  MOCK_METHOD2(sendLocalReply, void(const DirectResponse&, bool));
  MOCK_METHOD2(startUpstreamResponse, void(Transport&, Protocol&));
  MOCK_METHOD1(upstreamData, ResponseStatus(Buffer::Instance&));
  MOCK_METHOD0(resetDownstreamConnection, void());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());

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

  MOCK_CONST_METHOD0(stage, uint32_t());
  MOCK_CONST_METHOD0(disableKey, const std::string&());
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

  MOCK_CONST_METHOD0(empty, bool());
  MOCK_CONST_METHOD1(
      getApplicableRateLimit,
      const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&(uint32_t));

  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limit_policy_entry_;
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // ThriftProxy::Router::RouteEntry
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD0(metadataMatchCriteria, const Envoy::Router::MetadataMatchCriteria*());
  MOCK_CONST_METHOD0(tlsContextMatchCriteria, const Envoy::Router::TlsContextMatchCriteria*());
  MOCK_CONST_METHOD0(rateLimitPolicy, RateLimitPolicy&());

  std::string cluster_name_{"fake_cluster"};
  NiceMock<MockRateLimitPolicy> rate_limit_policy_;
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // ThriftProxy::Router::Route
  MOCK_CONST_METHOD0(routeEntry, const RouteEntry*());

  NiceMock<MockRouteEntry> route_entry_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
