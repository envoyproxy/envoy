#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/network/meta_protocol_proxy/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

/**
 * Decoder callback of request.
 */
class RequestDecoderCallback {
public:
  virtual ~RequestDecoderCallback() = default;

  virtual void onRequest(RequestPtr request) PURE;
  virtual void onDirectResponse(ResponsePtr direct) PURE;
  virtual void onDecodingError() PURE;
};

/**
 * Decoder callback of Response.
 */
class ResponseDecoderCallback {
public:
  virtual ~ResponseDecoderCallback() = default;

  virtual void onResponse(ResponsePtr response) PURE;
  virtual void onDecodingError() PURE;
};

/**
 * Decoder of request.
 */
class RequestDecoder {
public:
  virtual ~RequestDecoder() = default;

  virtual void setDecoderCallback(RequestDecoderCallback& callback) PURE;
  virtual void decode(Buffer::Instance& buffer) PURE;
};

/**
 * Decoder of response.
 */
class ResponseDecoder {
public:
  virtual ~ResponseDecoder() = default;

  virtual void setDecoderCallback(ResponseDecoderCallback& callback) PURE;
  virtual void decode(Buffer::Instance& buffer) PURE;
};

/*
 * Encoder of request.
 */
class RequestEncoder {
public:
  virtual ~RequestEncoder() = default;

  // TODO(wbpcode): update this method to support async encoding.
  virtual void encode(const Request&, Buffer::Instance& buffer) PURE;
};

/*
 * Encoder of response.
 */
class ResponseEncoder {
public:
  virtual ~ResponseEncoder() = default;

  // TODO(wbpcode): update this method to support async encoding.
  virtual void encode(const Response&, Buffer::Instance& buffer) PURE;
};

class MessageCreator {
public:
  virtual ~MessageCreator() = default;

  /**
   * Create local response message for local reply.
   */
  virtual ResponsePtr response(Status status, absl::string_view status_detail,
                               Request* origin_request = nullptr) PURE;
};

using RequestDecoderPtr = std::unique_ptr<RequestDecoder>;
using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;
using RequestEncoderPtr = std::unique_ptr<RequestEncoder>;
using ResponseEncoderPtr = std::unique_ptr<ResponseEncoder>;
using MessageCreatorPtr = std::unique_ptr<MessageCreator>;

/**
 * Factory used to create meta protocol stream encoder and decoder. If the developer wants to add
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

  std::string category() const override { return "envoy.meta_protocol_proxy.codec"; }
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
