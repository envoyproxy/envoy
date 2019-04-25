#pragma once

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class MockDecoderEventHandler : public DecoderEventHandler {
public:
  MockDecoderEventHandler();

  MOCK_METHOD0(transportBegin, Network::FilterStatus());
  MOCK_METHOD0(transportEnd, Network::FilterStatus());
  MOCK_METHOD3(messageBegin, Network::FilterStatus(MessageType, int64_t, SerializationType));
  MOCK_METHOD1(messageEnd, Network::FilterStatus(MessageMetadataSharedPtr));
  MOCK_METHOD2(transferHeaderTo, Network::FilterStatus(Buffer::Instance&, size_t));
  MOCK_METHOD2(transferBodyTo, Network::FilterStatus(Buffer::Instance&, size_t));
};

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  MockDecoderCallbacks();
  ~MockDecoderCallbacks() = default;

  MOCK_METHOD0(newDecoderEventHandler, DecoderEventHandler*());
  MOCK_METHOD1(onHeartbeat, void(MessageMetadataSharedPtr));

  MockDecoderEventHandler handler_;
};

class MockProtocolCallbacks : public ProtocolCallbacks {
public:
  MockProtocolCallbacks() = default;
  ~MockProtocolCallbacks() = default;

  void onRequestMessage(RequestMessagePtr&& req) override { onRequestMessageRvr(req.get()); }
  void onResponseMessage(ResponseMessagePtr&& res) override { onResponseMessageRvr(res.get()); }

  // DubboProxy::ProtocolCallbacks
  MOCK_METHOD1(onRequestMessageRvr, void(RequestMessage*));
  MOCK_METHOD1(onResponseMessageRvr, void(ResponseMessage*));
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol();

  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(type, ProtocolType());
  MOCK_METHOD2(decode, bool(Buffer::Instance&, Context*));
  MOCK_METHOD3(decode, bool(Buffer::Instance&, Protocol::Context*, MessageMetadataSharedPtr));
  MOCK_METHOD3(encode, bool(Buffer::Instance&, int32_t, const MessageMetadata&));

  std::string name_{"MockProtocol"};
  ProtocolType type_{ProtocolType::Dubbo};
};

class MockDeserializer : public Deserializer {
public:
  MockDeserializer();
  ~MockDeserializer();

  // DubboProxy::Deserializer
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(type, SerializationType());
  MOCK_METHOD3(deserializeRpcInvocation, void(Buffer::Instance&, size_t, MessageMetadataSharedPtr));
  MOCK_METHOD2(deserializeRpcResult, RpcResultPtr(Buffer::Instance&, size_t));
  MOCK_METHOD3(serializeRpcResult, size_t(Buffer::Instance&, const std::string&, RpcResponseType));

  std::string name_{"mockDeserializer"};
  SerializationType type_{SerializationType::Hessian};
};

namespace Router {
class MockRoute;
} // namespace Router

namespace DubboFilters {

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks();

  MOCK_METHOD1(addDecoderFilter, void(DecoderFilterSharedPtr));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter();

  // DubboProxy::DubboFilters::DecoderFilter
  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD1(setDecoderFilterCallbacks, void(DecoderFilterCallbacks& callbacks));

  // DubboProxy::DecoderEventHandler
  MOCK_METHOD0(transportBegin, Network::FilterStatus());
  MOCK_METHOD0(transportEnd, Network::FilterStatus());
  MOCK_METHOD3(messageBegin, Network::FilterStatus(MessageType, int64_t, SerializationType));
  MOCK_METHOD1(messageEnd, Network::FilterStatus(MessageMetadataSharedPtr));
  MOCK_METHOD2(transferHeaderTo, Network::FilterStatus(Buffer::Instance& buf, size_t size));
  MOCK_METHOD2(transferBodyTo, Network::FilterStatus(Buffer::Instance& buf, size_t size));
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks();

  // DubboProxy::DubboFilters::DecoderFilterCallbacks
  MOCK_CONST_METHOD0(requestId, uint64_t());
  MOCK_CONST_METHOD0(streamId, uint64_t());
  MOCK_CONST_METHOD0(connection, const Network::Connection*());
  MOCK_METHOD0(continueDecoding, void());
  MOCK_METHOD0(route, Router::RouteConstSharedPtr());
  MOCK_CONST_METHOD0(downstreamSerializationType, SerializationType());
  MOCK_CONST_METHOD0(downstreamProtocolType, ProtocolType());
  MOCK_METHOD2(sendLocalReply, void(const DirectResponse&, bool));
  MOCK_METHOD2(startUpstreamResponse, void(Deserializer&, Protocol&));
  MOCK_METHOD1(upstreamData, UpstreamResponseStatus(Buffer::Instance&));
  MOCK_METHOD0(resetDownstreamConnection, void());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_METHOD0(resetStream, void());

  uint64_t stream_id_{1};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Router::MockRoute> route_;
};

class MockDirectResponse : public DirectResponse {
public:
  MockDirectResponse();
  ~MockDirectResponse();

  MOCK_CONST_METHOD4(encode, DirectResponse::ResponseType(MessageMetadata&, Protocol&,
                                                          Deserializer&, Buffer::Instance&));
};

template <class ConfigProto> class MockFactoryBase : public NamedDubboFilterConfigFactory {
public:
  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    const auto& typed_config = dynamic_cast<const ConfigProto&>(proto_config);
    return createFilterFactoryFromProtoTyped(typed_config, stats_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() override { return name_; }

protected:
  MockFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;

  const std::string name_;
};

class MockFilterConfigFactory : public MockFactoryBase<ProtobufWkt::Struct> {
public:
  MockFilterConfigFactory();
  ~MockFilterConfigFactory();

  DubboFilters::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProtobufWkt::Struct& proto_config,
                                    const std::string& stat_prefix,
                                    Server::Configuration::FactoryContext& context) override;

  std::shared_ptr<MockDecoderFilter> mock_filter_;
  ProtobufWkt::Struct config_struct_;
  std::string config_stat_prefix_;
};

} // namespace DubboFilters

namespace Router {

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry();

  // DubboProxy::Router::RouteEntry
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD0(metadataMatchCriteria, const Envoy::Router::MetadataMatchCriteria*());

  std::string cluster_name_{"fake_cluster"};
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute();

  // DubboProxy::Router::Route
  MOCK_CONST_METHOD0(routeEntry, const RouteEntry*());

  NiceMock<MockRouteEntry> route_entry_;
};

} // namespace Router

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
