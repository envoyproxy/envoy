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

  MOCK_METHOD(void, onDecodingSuccess, (RequestHeaderFramePtr, absl::optional<StartTime>));
  MOCK_METHOD(void, onDecodingSuccess, (RequestCommonFramePtr));
  MOCK_METHOD(void, onDecodingFailure, (absl::string_view));
  MOCK_METHOD(void, writeToConnection, (Buffer::Instance&));

  MOCK_METHOD(OptRef<Network::Connection>, connection, ());
};

class MockClientCodecCallbacks : public ClientCodecCallbacks {
public:
  MockClientCodecCallbacks() {
    ON_CALL(*this, writeToConnection(_))
        .WillByDefault(
            testing::Invoke([](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));
  }

  MOCK_METHOD(void, onDecodingSuccess, (ResponseHeaderFramePtr, absl::optional<StartTime>));
  MOCK_METHOD(void, onDecodingSuccess, (ResponseCommonFramePtr));
  MOCK_METHOD(void, onDecodingFailure, (absl::string_view));
  MOCK_METHOD(void, writeToConnection, (Buffer::Instance & buffer));
  MOCK_METHOD(OptRef<Network::Connection>, connection, ());
  MOCK_METHOD(OptRef<const Upstream::ClusterInfo>, upstreamCluster, (), (const));
};

class MockEncodingContext : public EncodingContext {
public:
  MOCK_METHOD(OptRef<const RouteEntry>, routeEntry, (), (const));
};

class MockServerCodec : public ServerCodec {
public:
  MockServerCodec() {
    ON_CALL(*this, encode(_, _)).WillByDefault(testing::Return(EncodingResult{0}));
  }

  MOCK_METHOD(void, setCodecCallbacks, (ServerCodecCallbacks & callbacks));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer, bool end_stream));
  MOCK_METHOD(EncodingResult, encode, (const StreamFrame&, EncodingContext& ctx));
  MOCK_METHOD(ResponseHeaderFramePtr, respond,
              (Status status, absl::string_view, const RequestHeaderFrame&));
};

class MockClientCodec : public ClientCodec {
public:
  MockClientCodec() {
    ON_CALL(*this, encode(_, _)).WillByDefault(testing::Return(EncodingResult{0}));
  }

  MOCK_METHOD(void, setCodecCallbacks, (ClientCodecCallbacks & callbacks));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer, bool end_stream));
  MOCK_METHOD(EncodingResult, encode, (const StreamFrame&, EncodingContext& ctx));
};

class MockCodecFactory : public CodecFactory {
public:
  MockCodecFactory();

  MOCK_METHOD(ServerCodecPtr, createServerCodec, (), (const));
  MOCK_METHOD(ClientCodecPtr, createClientCodec, (), (const));
};

class MockProxyFactory : public ProxyFactory {
public:
  MockProxyFactory();

  MOCK_METHOD(void, createProxy,
              (Server::Configuration::FactoryContext&, Network::FilterManager&,
               FilterConfigSharedPtr),
              (const));
};

class MockStreamCodecFactoryConfig : public CodecFactoryConfig {
public:
  MockStreamCodecFactoryConfig();

  MOCK_METHOD(CodecFactoryPtr, createCodecFactory,
              (const Protobuf::Message&, Server::Configuration::ServerFactoryContext&));
  MOCK_METHOD(ProxyFactoryPtr, createProxyFactory,
              (const Protobuf::Message&, Server::Configuration::ServerFactoryContext&));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
  std::set<std::string> configTypes() override { return {"envoy.generic_proxy.codecs.mock.type"}; }
  std::string name() const override { return "envoy.generic_proxy.codecs.mock"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
