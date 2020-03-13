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

  MOCK_METHOD(FilterStatus, onMessageDecoded, (MessageMetadataSharedPtr, ContextSharedPtr));
};

class MockStreamEncoder : public StreamEncoder {
public:
  MockStreamEncoder();

  MOCK_METHOD(FilterStatus, onMessageEncoded, (MessageMetadataSharedPtr, ContextSharedPtr));
};

class MockStreamHandler : public StreamHandler {
public:
  MockStreamHandler() = default;

  MOCK_METHOD(void, onStreamDecoded, (MessageMetadataSharedPtr, ContextSharedPtr));
};

class MockRequestDecoderCallbacks : public RequestDecoderCallbacks {
public:
  MockRequestDecoderCallbacks();
  ~MockRequestDecoderCallbacks() override = default;

  MOCK_METHOD(StreamHandler&, newStream, ());
  MOCK_METHOD(void, onHeartbeat, (MessageMetadataSharedPtr));

  MockStreamHandler handler_;
};
class MockResponseDecoderCallbacks : public ResponseDecoderCallbacks {
public:
  MockResponseDecoderCallbacks();
  ~MockResponseDecoderCallbacks() override = default;

  MOCK_METHOD(StreamHandler&, newStream, ());
  MOCK_METHOD(void, onHeartbeat, (MessageMetadataSharedPtr));

  MockStreamHandler handler_;
};

class MockActiveStream : public ActiveStream {
public:
  MockActiveStream(StreamHandler& handler, MessageMetadataSharedPtr metadata,
                   ContextSharedPtr context)
      : ActiveStream(handler, metadata, context) {}
  ~MockActiveStream() = default;

  MOCK_METHOD(ActiveStream*, newStream, (MessageMetadataSharedPtr, ContextSharedPtr));
  MOCK_METHOD(void, onHeartbeat, (MessageMetadataSharedPtr));
};

class MockDecoderStateMachineDelegate : public DecoderStateMachine::Delegate {
public:
  MockDecoderStateMachineDelegate() = default;
  ~MockDecoderStateMachineDelegate() override = default;

  MOCK_METHOD(ActiveStream*, newStream, (MessageMetadataSharedPtr, ContextSharedPtr));
  MOCK_METHOD(void, onHeartbeat, (MessageMetadataSharedPtr));
};

class MockSerializer : public Serializer {
public:
  MockSerializer();
  ~MockSerializer() override;

  // DubboProxy::Serializer
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(SerializationType, type, (), (const));
  MOCK_METHOD((std::pair<RpcInvocationSharedPtr, bool>), deserializeRpcInvocation,
              (Buffer::Instance&, ContextSharedPtr));
  MOCK_METHOD((std::pair<RpcResultSharedPtr, bool>), deserializeRpcResult,
              (Buffer::Instance&, ContextSharedPtr));
  MOCK_METHOD(size_t, serializeRpcResult, (Buffer::Instance&, const std::string&, RpcResponseType));

  std::string name_{"mockDeserializer"};
  SerializationType type_{SerializationType::Hessian2};
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol() override;

  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(ProtocolType, type, (), (const));
  MOCK_METHOD(Serializer*, serializer, (), (const));
  MOCK_METHOD((std::pair<ContextSharedPtr, bool>), decodeHeader,
              (Buffer::Instance&, MessageMetadataSharedPtr));
  MOCK_METHOD(bool, decodeData, (Buffer::Instance&, ContextSharedPtr, MessageMetadataSharedPtr));
  MOCK_METHOD(bool, encode,
              (Buffer::Instance&, const MessageMetadata&, const std::string&, RpcResponseType));

  std::string name_{"MockProtocol"};
  ProtocolType type_{ProtocolType::Dubbo};
  NiceMock<MockSerializer> serializer_;
};

class MockNamedSerializerConfigFactory : public NamedSerializerConfigFactory {
public:
  MockNamedSerializerConfigFactory(std::function<MockSerializer*()> f) : f_(f) {}

  SerializerPtr createSerializer() override { return SerializerPtr{f_()}; }
  std::string name() const override {
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
  std::string name() const override { return ProtocolNames::get().fromType(ProtocolType::Dubbo); }

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

  MOCK_METHOD(void, createFilterChain, (DubboFilters::FilterChainFactoryCallbacks & callbacks));
};

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD(void, addDecoderFilter, (DecoderFilterSharedPtr));
  MOCK_METHOD(void, addEncoderFilter, (EncoderFilterSharedPtr));
  MOCK_METHOD(void, addFilter, (CodecFilterSharedPtr));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter() override;

  // DubboProxy::DubboFilters::DecoderFilter
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks));
  MOCK_METHOD(FilterStatus, onMessageDecoded, (MessageMetadataSharedPtr, ContextSharedPtr));

  DecoderFilterCallbacks* callbacks_{};
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks() override;

  // DubboProxy::DubboFilters::DecoderFilterCallbacks
  MOCK_METHOD(uint64_t, requestId, (), (const));
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(SerializationType, serializationType, (), (const));
  MOCK_METHOD(ProtocolType, protocolType, (), (const));
  MOCK_METHOD(void, sendLocalReply, (const DirectResponse&, bool));
  MOCK_METHOD(void, startUpstreamResponse, ());
  MOCK_METHOD(UpstreamResponseStatus, upstreamData, (Buffer::Instance&));
  MOCK_METHOD(void, resetDownstreamConnection, ());
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());

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
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallbacks & callbacks));
  MOCK_METHOD(FilterStatus, onMessageEncoded, (MessageMetadataSharedPtr, ContextSharedPtr));

  EncoderFilterCallbacks* callbacks_{};
};

class MockEncoderFilterCallbacks : public EncoderFilterCallbacks {
public:
  MockEncoderFilterCallbacks();
  ~MockEncoderFilterCallbacks() override;

  // DubboProxy::DubboFilters::MockEncoderFilterCallbacks
  MOCK_METHOD(uint64_t, requestId, (), (const));
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(SerializationType, serializationType, (), (const));
  MOCK_METHOD(ProtocolType, protocolType, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, continueEncoding, ());
  MOCK_METHOD(void, continueDecoding, ());

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

  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallbacks & callbacks));
  MOCK_METHOD(FilterStatus, onMessageEncoded, (MessageMetadataSharedPtr, ContextSharedPtr));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks));
  MOCK_METHOD(FilterStatus, onMessageDecoded, (MessageMetadataSharedPtr, ContextSharedPtr));

  DecoderFilterCallbacks* decoder_callbacks_{};
  EncoderFilterCallbacks* encoder_callbacks_{};
};

class MockDirectResponse : public DirectResponse {
public:
  MockDirectResponse() = default;
  ~MockDirectResponse() override = default;

  MOCK_METHOD(DirectResponse::ResponseType, encode,
              (MessageMetadata&, Protocol&, Buffer::Instance&), (const));
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

  std::string name() const override { return name_; }

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
  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(const Envoy::Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (const));

  std::string cluster_name_{"fake_cluster"};
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // DubboProxy::Router::Route
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));

  NiceMock<MockRouteEntry> route_entry_;
};

} // namespace Router

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
