#pragma once

#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class MockRequestDecoderCallback : public RequestDecoderCallback {
public:
  MOCK_METHOD(void, onDecodingSuccess, (RequestPtr request));
  MOCK_METHOD(void, onDecodingFailure, ());
};

class MockResponseDecoderCallback : public ResponseDecoderCallback {
public:
  MOCK_METHOD(void, onDecodingSuccess, (ResponsePtr response));
  MOCK_METHOD(void, onDecodingFailure, ());
};

class MockRequestEncoderCallback : public RequestEncoderCallback {
public:
  MOCK_METHOD(void, onEncodingSuccess, (Buffer::Instance & buffer, bool expect_response));
};

/**
 * Encoder callback of Response.
 */
class MockResponseEncoderCallback : public ResponseEncoderCallback {
public:
  MOCK_METHOD(void, onEncodingSuccess, (Buffer::Instance & buffer, bool close_connection));
};

class MockRequestDecoder : public RequestDecoder {
public:
  MOCK_METHOD(void, setDecoderCallback, (RequestDecoderCallback & callback));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer));
};

class MockResponseDecoder : public ResponseDecoder {
public:
  MOCK_METHOD(void, setDecoderCallback, (ResponseDecoderCallback & callback));
  MOCK_METHOD(void, decode, (Buffer::Instance & buffer));
};

class MockRequestEncoder : public RequestEncoder {
public:
  MOCK_METHOD(void, encode, (const Request&, RequestEncoderCallback& callback));
};

class MockResponseEncoder : public ResponseEncoder {
public:
  MOCK_METHOD(void, encode, (const Response&, ResponseEncoderCallback& callback));
};

class MockMessageCreator : public MessageCreator {
public:
  MOCK_METHOD(ResponsePtr, response, (Status status, const Request&));
};

class MockCodecFactory : public CodecFactory {
public:
  MockCodecFactory();

  MOCK_METHOD(RequestDecoderPtr, requestDecoder, (), (const));
  MOCK_METHOD(ResponseDecoderPtr, responseDecoder, (), (const));
  MOCK_METHOD(RequestEncoderPtr, requestEncoder, (), (const));
  MOCK_METHOD(ResponseEncoderPtr, responseEncoder, (), (const));
  MOCK_METHOD(MessageCreatorPtr, messageCreator, (), (const));
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
