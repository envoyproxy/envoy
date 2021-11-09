#pragma once

#include "source/extensions/filters/network/meta_protocol_proxy/interface/codec.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

class MockRequestDecoderCallback : public RequestDecoderCallback {
public:
  MOCK_METHOD(void, onRequest, (RequestPtr request));
  MOCK_METHOD(void, onDirectResponse, (ResponsePtr direct));
  MOCK_METHOD(void, onDecodingError, ());
};

class MockResponseDecoderCallback : public ResponseDecoderCallback {
public:
  MOCK_METHOD(void, onResponse, (ResponsePtr response));
  MOCK_METHOD(void, onDecodingError, ());
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
  MOCK_METHOD(void, encode, (const Request&, Buffer::Instance& buffer));
};

class MockResponseEncoder : public ResponseEncoder {
public:
  MOCK_METHOD(void, encode, (const Response&, Buffer::Instance& buffer));
};

class MockMessageCreator : public MessageCreator {
public:
  MOCK_METHOD(ResponsePtr, response,
              (Status status, absl::string_view status_detail, Request* origin_request));
};

class MockCodecFactory : CodecFactory {
public:
  MockCodecFactory();

  MOCK_METHOD(RequestDecoderPtr, requestDecoder, (), (const));
  MOCK_METHOD(ResponseDecoderPtr, responseDecoder, (), (const));
  MOCK_METHOD(RequestEncoderPtr, requestEncoder, (), (const));
  MOCK_METHOD(ResponseEncoderPtr, responseEncoder, (), (const));
  MOCK_METHOD(MessageCreatorPtr, messageCreator, (), (const));
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
