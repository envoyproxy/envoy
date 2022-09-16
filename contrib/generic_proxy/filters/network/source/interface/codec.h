#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Decoder callback of request.
 */
class RequestDecoderCallback {
public:
  virtual ~RequestDecoderCallback() = default;

  /**
   * If request decoding success then this method will be called.
   * @param request request from decoding.
   */
  virtual void onDecodingSuccess(RequestPtr request) PURE;

  /**
   * If request decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;
};

/**
 * Decoder callback of Response.
 */
class ResponseDecoderCallback {
public:
  virtual ~ResponseDecoderCallback() = default;

  /**
   * If response decoding success then this method will be called.
   * @param response response from decoding.
   */
  virtual void onDecodingSuccess(ResponsePtr response) PURE;

  /**
   * If response decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;
};

/**
 * Encoder callback of request.
 */
class RequestEncoderCallback {
public:
  virtual ~RequestEncoderCallback() = default;

  /**
   * If request encoding success then this method will be called.
   * @param buffer encoding result buffer.
   * @param expect_response whether the current request requires an upstream response.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer, bool expect_response) PURE;
};

/**
 * Encoder callback of Response.
 */
class ResponseEncoderCallback {
public:
  virtual ~ResponseEncoderCallback() = default;

  /**
   * If response encoding success then this method will be called.
   * @param buffer encoding result buffer.
   * @param close_connection whether the downstream connection should be closed.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer, bool close_connection) PURE;
};

/**
 * Decoder of request.
 */
class RequestDecoder {
public:
  virtual ~RequestDecoder() = default;

  // The decode() method may be called multiple times for single request or response.
  // So an independent setDecoderCallback() is used to set decoding callback.
  virtual void setDecoderCallback(RequestDecoderCallback& callback) PURE;
  virtual void decode(Buffer::Instance& buffer) PURE;
};

/**
 * Decoder of response.
 */
class ResponseDecoder {
public:
  virtual ~ResponseDecoder() = default;

  // The decode() method may be called multiple times for single request or response.
  // So an independent setDecoderCallback() is used to set decoding callback.
  virtual void setDecoderCallback(ResponseDecoderCallback& callback) PURE;
  virtual void decode(Buffer::Instance& buffer) PURE;
};

/*
 * Encoder of request.
 */
class RequestEncoder {
public:
  virtual ~RequestEncoder() = default;

  virtual void encode(const Request&, RequestEncoderCallback& callback) PURE;
};

/*
 * Encoder of response.
 */
class ResponseEncoder {
public:
  virtual ~ResponseEncoder() = default;

  virtual void encode(const Response&, ResponseEncoderCallback& callback) PURE;
};

class MessageCreator {
public:
  virtual ~MessageCreator() = default;

  /**
   * Create local response message for local reply.
   */
  virtual ResponsePtr response(Status status, const Request& origin_request) PURE;
};

using RequestDecoderPtr = std::unique_ptr<RequestDecoder>;
using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;
using RequestEncoderPtr = std::unique_ptr<RequestEncoder>;
using ResponseEncoderPtr = std::unique_ptr<ResponseEncoder>;
using MessageCreatorPtr = std::unique_ptr<MessageCreator>;

/**
 * Factory used to create generic stream encoder and decoder. If the developer wants to add
 * new protocol support to this proxy, they need to implement the corresponding codec factory for
 * the corresponding protocol.
 */
class CodecFactory {
public:
  virtual ~CodecFactory() = default;

  /*
   * Create request decoder.
   */
  virtual RequestDecoderPtr requestDecoder() const PURE;

  /*
   * Create response decoder.
   */
  virtual ResponseDecoderPtr responseDecoder() const PURE;

  /*
   * Create request encoder.
   */
  virtual RequestEncoderPtr requestEncoder() const PURE;

  /*
   * Create response encoder.
   */
  virtual ResponseEncoderPtr responseEncoder() const PURE;

  /**
   * Create message creator.
   */
  virtual MessageCreatorPtr messageCreator() const PURE;
};

using CodecFactoryPtr = std::unique_ptr<CodecFactory>;

/**
 * Factory config for codec factory. This class is used to register and create codec factories.
 */
class CodecFactoryConfig : public Envoy::Config::TypedFactory {
public:
  virtual CodecFactoryPtr createFactory(const Protobuf::Message& config,
                                        Envoy::Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.generic_proxy.codec"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
