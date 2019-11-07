#pragma once

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/dubbo_proxy/decoder.h"
#include "extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class MockStreamDecoder : public StreamDecoder {
public:
  MockStreamDecoder();

  MOCK_METHOD2(onMessageDecoded, FilterStatus(MessageMetadataSharedPtr, ContextSharedPtr));
};

class MockStreamEncoder : public StreamEncoder {
public:
  MockStreamEncoder();

  MOCK_METHOD2(onMessageEncoded, FilterStatus(MessageMetadataSharedPtr, ContextSharedPtr));
};

class MockStreamHandler : public StreamHandler {
public:
  MockStreamHandler() = default;

  MOCK_METHOD2(onStreamDecoded, void(MessageMetadataSharedPtr, ContextSharedPtr));
};

class MockRequestDecoderCallbacks : public RequestDecoderCallbacks {
public:
  MockRequestDecoderCallbacks();
  ~MockRequestDecoderCallbacks() override = default;

  MOCK_METHOD0(newStream, StreamHandler&());
  MOCK_METHOD1(onHeartbeat, void(MessageMetadataSharedPtr));

  MockStreamHandler handler_;
};
class MockResponseDecoderCallbacks : public ResponseDecoderCallbacks {
public:
  MockResponseDecoderCallbacks();
  ~MockResponseDecoderCallbacks() override = default;

  MOCK_METHOD0(newStream, StreamHandler&());
  MOCK_METHOD1(onHeartbeat, void(MessageMetadataSharedPtr));

  MockStreamHandler handler_;
};

class MockActiveStream : public ActiveStream {
public:
  MockActiveStream(StreamHandler& handler, MessageMetadataSharedPtr metadata,
                   ContextSharedPtr context)
      : ActiveStream(handler, metadata, context) {}
  ~MockActiveStream() = default;

  MOCK_METHOD2(newStream, ActiveStream*(MessageMetadataSharedPtr, ContextSharedPtr));
  MOCK_METHOD1(onHeartbeat, void(MessageMetadataSharedPtr));
};

class MockDecoderStateMachineDelegate : public DecoderStateMachine::Delegate {
public:
  MockDecoderStateMachineDelegate() = default;
  ~MockDecoderStateMachineDelegate() override = default;

  MOCK_METHOD2(newStream, ActiveStream*(MessageMetadataSharedPtr, ContextSharedPtr));
  MOCK_METHOD1(onHeartbeat, void(MessageMetadataSharedPtr));
};

class MockSerializer : public Serializer {
public:
  MockSerializer();
  ~MockSerializer() override;

  // DubboProxy::Serializer
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(type, SerializationType());
  MOCK_METHOD2(deserializeRpcInvocation,
               std::pair<RpcInvocationSharedPtr, bool>(Buffer::Instance&, ContextSharedPtr));
  MOCK_METHOD2(deserializeRpcResult,
               std::pair<RpcResultSharedPtr, bool>(Buffer::Instance&, ContextSharedPtr));
  MOCK_METHOD3(serializeRpcResult, size_t(Buffer::Instance&, const std::string&, RpcResponseType));

  std::string name_{"mockDeserializer"};
  SerializationType type_{SerializationType::Hessian2};
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol() override;

  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(type, ProtocolType());
  MOCK_CONST_METHOD0(serializer, Serializer*());
  MOCK_METHOD2(decodeHeader,
               std::pair<ContextSharedPtr, bool>(Buffer::Instance&, MessageMetadataSharedPtr));
  MOCK_METHOD3(decodeData, bool(Buffer::Instance&, ContextSharedPtr, MessageMetadataSharedPtr));
  MOCK_METHOD4(encode, bool(Buffer::Instance&, const MessageMetadata&, const std::string&,
                            RpcResponseType));

  std::string name_{"MockProtocol"};
  ProtocolType type_{ProtocolType::Dubbo};
  NiceMock<MockSerializer> serializer_;
};

class MockNamedSerializerConfigFactory : public NamedSerializerConfigFactory {
public:
  MockNamedSerializerConfigFactory(std::function<MockSerializer*()> f) : f_(f) {}

  SerializerPtr createSerializer() override { return SerializerPtr{f_()}; }
  std::string name() override {
    return SerializerNames::get().fromType(SerializationType::Hessian2);
  }

  std::function<MockSerializer*()> f_;
};

class MockNamedProtocolConfigFactory : public NamedProtocolConfigFactory {
public:
  MockNamedProtocolConfigFactory(std::function<MockProtocol*()> f) : f_(f) {}

  ProtocolPtr createProtocol(SerializationType serialization_type) override {
    auto protocol = ProtocolPtr{f_()};
    protocol->initSerializer(serialization_type);
    return protocol;
  }
  std::string name() override { return ProtocolNames::get().fromType(ProtocolType::Dubbo); }

  std::function<MockProtocol*()> f_;
};

namespace Router {
class MockRoute;
} // namespace Router

namespace DubboFilters {

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory() override;

  MOCK_METHOD1(createFilterChain, void(DubboFilters::FilterChainFactoryCallbacks& callbacks));
};

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD1(addDecoderFilter, void(DecoderFilterSharedPtr));
  MOCK_METHOD1(addEncoderFilter, void(EncoderFilterSharedPtr));
  MOCK_METHOD1(addFilter, void(CodecFilterSharedPtr));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter() override;

  // DubboProxy::DubboFilters::DecoderFilter
  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD1(setDecoderFilterCallbacks, void(DecoderFilterCallbacks& callbacks));
  MOCK_METHOD2(onMessageDecoded, FilterStatus(MessageMetadataSharedPtr, ContextSharedPtr));

  DecoderFilterCallbacks* callbacks_{};
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks() override;

  // DubboProxy::DubboFilters::DecoderFilterCallbacks
  MOCK_CONST_METHOD0(requestId, uint64_t());
  MOCK_CONST_METHOD0(streamId, uint64_t());
  MOCK_CONST_METHOD0(connection, const Network::Connection*());
  MOCK_METHOD0(continueDecoding, void());
  MOCK_METHOD0(route, Router::RouteConstSharedPtr());
  MOCK_CONST_METHOD0(serializationType, SerializationType());
  MOCK_CONST_METHOD0(protocolType, ProtocolType());
  MOCK_METHOD2(sendLocalReply, void(const DirectResponse&, bool));
  MOCK_METHOD0(startUpstreamResponse, void());
  MOCK_METHOD1(upstreamData, UpstreamResponseStatus(Buffer::Instance&));
  MOCK_METHOD0(resetDownstreamConnection, void());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_METHOD0(resetStream, void());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());

  uint64_t stream_id_{1};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Router::MockRoute> route_;
  NiceMock<Event::MockDispatcher> dispatcher_;
};

class MockEncoderFilter : public EncoderFilter {
public:
  MockEncoderFilter();
  ~MockEncoderFilter() override;

  // DubboProxy::DubboFilters::EncoderFilter
  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD1(setEncoderFilterCallbacks, void(EncoderFilterCallbacks& callbacks));
  MOCK_METHOD2(onMessageEncoded, FilterStatus(MessageMetadataSharedPtr, ContextSharedPtr));

  EncoderFilterCallbacks* callbacks_{};
};

class MockEncoderFilterCallbacks : public EncoderFilterCallbacks {
public:
  MockEncoderFilterCallbacks();
  ~MockEncoderFilterCallbacks() override;

  // DubboProxy::DubboFilters::MockEncoderFilterCallbacks
  MOCK_CONST_METHOD0(requestId, uint64_t());
  MOCK_CONST_METHOD0(streamId, uint64_t());
  MOCK_CONST_METHOD0(connection, const Network::Connection*());
  MOCK_METHOD0(route, Router::RouteConstSharedPtr());
  MOCK_CONST_METHOD0(serializationType, SerializationType());
  MOCK_CONST_METHOD0(protocolType, ProtocolType());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_METHOD0(resetStream, void());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(continueEncoding, void());
  MOCK_METHOD0(continueDecoding, void());

  uint64_t stream_id_{1};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Router::MockRoute> route_;
  NiceMock<Event::MockDispatcher> dispatcher_;
};

class MockCodecFilter : public CodecFilter {
public:
  MockCodecFilter();
  ~MockCodecFilter() override;

  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD1(setEncoderFilterCallbacks, void(EncoderFilterCallbacks& callbacks));
  MOCK_METHOD2(onMessageEncoded, FilterStatus(MessageMetadataSharedPtr, ContextSharedPtr));
  MOCK_METHOD1(setDecoderFilterCallbacks, void(DecoderFilterCallbacks& callbacks));
  MOCK_METHOD2(onMessageDecoded, FilterStatus(MessageMetadataSharedPtr, ContextSharedPtr));

  DecoderFilterCallbacks* decoder_callbacks_{};
  EncoderFilterCallbacks* encoder_callbacks_{};
};

class MockDirectResponse : public DirectResponse {
public:
  MockDirectResponse() = default;
  ~MockDirectResponse() override = default;

  MOCK_CONST_METHOD3(encode,
                     DirectResponse::ResponseType(MessageMetadata&, Protocol&, Buffer::Instance&));
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
  ~MockFilterConfigFactory() override;

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
  ~MockRouteEntry() override;

  // DubboProxy::Router::RouteEntry
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD0(metadataMatchCriteria, const Envoy::Router::MetadataMatchCriteria*());

  std::string cluster_name_{"fake_cluster"};
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // DubboProxy::Router::Route
  MOCK_CONST_METHOD0(routeEntry, const RouteEntry*());

  NiceMock<MockRouteEntry> route_entry_;
};

} // namespace Router

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
