#pragma once

#include "source/extensions/filters/network/generic_proxy/interface/codec.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using testing::_;

class MockServerCodecCallbacks : public ServerCodecCallbacks {
public:
  MockServerCodecCallbacks() {
    ON_CALL(*this, writeToConnection(_))
        .WillByDefault(
            testing::Invoke([](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));
  }

  MOCK_METHOD(void, onDecodingSuccess, (RequestHeaderFramePtr, std::optional<StartTime>),
              (override));
  MOCK_METHOD(void, onDecodingSuccess, (RequestCommonFramePtr), (override));
  MOCK_METHOD(void, onDecodingFailure, (absl::string_view), (override));
  MOCK_METHOD(void, writeToConnection, (Buffer::Instance&), (override));

  MOCK_METHOD(OptRef<Network::Connection>, connection, (), (override));
};

class MockClientCodecCallbacks : public ClientCodecCallbacks {
public:
  MockClientCodecCallbacks() {
    ON_CALL(*this, writeToConnection(_))
        .WillByDefault(
            testing::Invoke([](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));
  }

  MOCK_METHOD(void, onDecodingSuccess, (ResponseHeaderFramePtr, std::optional<StartTime>),
              (override));
  MOCK_METHOD(void, onDecodingSuccess, (ResponseCommonFramePtr), (override));
  MOCK_METHOD(void, onDecodingFailure, (absl::string_view), (override));
  MOCK_METHOD(void, writeToConnection, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(OptRef<Network::Connection>, connection, (), (override));
  MOCK_METHOD(OptRef<const Upstream::ClusterInfo>, upstreamCluster, (), (const, override));
};

class MockEncodingContext : public EncodingContext {
public:
  MOCK_METHOD(OptRef<const RouteEntry>, routeEntry, (), (const, override));
};

class MockServerCodec : public ServerCodec {
public:
  MockServerCodec() {
    ON_CALL(*this, encode(_, _)).WillByDefault(testing::Return(EncodingResult{0}));
  }

  MOCK_METHOD(void, setCodecCallbacks, (ServerCodecCallbacks & callbacks), (override));
  MOCK_METHOD(void, onConnected, (), (override));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer, bool end_stream), (override));
  MOCK_METHOD(EncodingResult, encode, (const StreamFrame&, EncodingContext& ctx), (override));
  MOCK_METHOD(ResponseHeaderFramePtr, respond,
              (Status status, absl::string_view, const RequestHeaderFrame&), (override));
};

class MockClientCodec : public ClientCodec {
public:
  MockClientCodec() {
    ON_CALL(*this, encode(_, _)).WillByDefault(testing::Return(EncodingResult{0}));
  }

  MOCK_METHOD(void, setCodecCallbacks, (ClientCodecCallbacks & callbacks), (override));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer, bool end_stream), (override));
  MOCK_METHOD(EncodingResult, encode, (const StreamFrame&, EncodingContext& ctx), (override));
};

class MockCodecFactory : public CodecFactory {
public:
  MockCodecFactory();

  MOCK_METHOD(ServerCodecPtr, createServerCodec, (), (const, override));
  MOCK_METHOD(ClientCodecPtr, createClientCodec, (), (const, override));
};

class MockProxyFactory : public ProxyFactory {
public:
  MockProxyFactory();

  MOCK_METHOD(void, createProxy,
              (Server::Configuration::FactoryContext&, Network::FilterManager&,
               FilterConfigSharedPtr),
              (const, override));
};

class MockStreamCodecFactoryConfig : public CodecFactoryConfig {
public:
  MockStreamCodecFactoryConfig();

  MOCK_METHOD(CodecFactoryPtr, createCodecFactory,
              (const Protobuf::Message&, Server::Configuration::ServerFactoryContext&), (override));
  MOCK_METHOD(ProxyFactoryPtr, createProxyFactory,
              (const Protobuf::Message&, Server::Configuration::ServerFactoryContext&), (override));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  std::set<std::string> configTypes() override { return {"envoy.generic_proxy.codecs.mock.type"}; }
  std::string name() const override { return "envoy.generic_proxy.codecs.mock"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
