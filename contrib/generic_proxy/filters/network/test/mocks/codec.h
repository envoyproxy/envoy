#pragma once

#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class MockServerCodecCallbacks : public ServerCodecCallbacks {
public:
  MOCK_METHOD(void, onDecodingSuccess, (StreamFramePtr request));
  MOCK_METHOD(void, onDecodingFailure, ());
  MOCK_METHOD(void, writeToConnection, (Buffer::Instance & buffer));
  MOCK_METHOD(OptRef<Network::Connection>, connection, ());
};

class MockClientCodecCallbacks : public ClientCodecCallbacks {
public:
  MOCK_METHOD(void, onDecodingSuccess, (StreamFramePtr response));
  MOCK_METHOD(void, onDecodingFailure, ());
  MOCK_METHOD(void, writeToConnection, (Buffer::Instance & buffer));
  MOCK_METHOD(OptRef<Network::Connection>, connection, ());
};

class MockEncodingCallbacks : public EncodingCallbacks {
public:
  MOCK_METHOD(void, onEncodingSuccess, (Buffer::Instance & buffer, bool end_stream));
};

class MockServerCodec : public ServerCodec {
public:
  MOCK_METHOD(void, setCodecCallbacks, (ServerCodecCallbacks & callbacks));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer, bool end_stream));
  MOCK_METHOD(void, encode, (const StreamFrame&, EncodingCallbacks& callbacks));
  MOCK_METHOD(ResponsePtr, respond, (Status status, absl::string_view, const Request&));
};

class MockClientCodec : public ClientCodec {
public:
  MOCK_METHOD(void, setCodecCallbacks, (ClientCodecCallbacks & callbacks));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer, bool end_stream));
  MOCK_METHOD(void, encode, (const StreamFrame&, EncodingCallbacks& callbacks));
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

  MOCK_METHOD(void, createProxy, (Network::FilterManager&, const FilterConfigSharedPtr&), (const));
};

class MockStreamCodecFactoryConfig : public CodecFactoryConfig {
public:
  MockStreamCodecFactoryConfig();

  MOCK_METHOD(CodecFactoryPtr, createCodecFactory,
              (const Protobuf::Message&, Envoy::Server::Configuration::FactoryContext&));
  MOCK_METHOD(ProxyFactoryPtr, createProxyFactory,
              (const Protobuf::Message&, Envoy::Server::Configuration::FactoryContext&));

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
