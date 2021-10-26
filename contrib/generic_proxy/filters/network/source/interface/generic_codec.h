#pragma once

#include "contrib/generic_proxy/filters/network/source/interface/generic_stream.h"

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Decoder callback of generic request.
 */
class RequestDecoderCallback {
public:
  virtual ~RequestDecoderCallback() = default;

  virtual void onGenericRequest(GenericRequestPtr request) PURE;
  virtual void onDirectResponse(GenericResponsePtr direct) PURE;
  virtual void onDecodingError() PURE;
};

/**
 * Decoder callback of generic Response.
 */
class ResponseDecoderCallback {
public:
  virtual ~ResponseDecoderCallback() = default;

  virtual void onGenericResponse(GenericResponsePtr response) PURE;
  virtual void onDecodingError() PURE;
};

/**
 * Decoder of generic request.
 */
class GenericRequestDecoder {
public:
  virtual ~GenericRequestDecoder() = default;

  virtual void setDecoderCallback(RequestDecoderCallback& callback) PURE;
  virtual void decode(Buffer::Instance& buffer) PURE;
};

/**
 * Decoder of generic respnose.
 */
class GenericResponseDecoder {
public:
  virtual ~GenericResponseDecoder() = default;

  virtual void setDecoderCallback(ResponseDecoderCallback& callback) PURE;
  virtual void decode(Buffer::Instance& buffer) PURE;
};

/*
 * Encoder of generic request.
 */
class GenericRequestEncoder {
public:
  virtual ~GenericRequestEncoder() = default;

  // TODO(wbpcode): update this method to support async encoding.
  virtual void encode(GenericRequest&, Buffer::Instance& buffer) PURE;
};

/*
 * Encoder of generic response.
 */
class GenericResponseEncoder {
public:
  virtual ~GenericResponseEncoder() = default;

  // TODO(wbpcode): update this method to support async encoding.
  virtual void encode(GenericResponse&, Buffer::Instance& buffer) PURE;
};

class GenericMessageCreator {
public:
  virtual ~GenericMessageCreator() = default;

  /**
   * Create local reponse message for local reply.
   */
  virtual GenericResponsePtr response(GenericState status, absl::string_view status_detail,
                                      GenericRequest* origin_request = nullptr) PURE;
};

using GenericRequestDecoderPtr = std::unique_ptr<GenericRequestDecoder>;
using GenericResponseDecoderPtr = std::unique_ptr<GenericResponseDecoder>;
using GenericRequestEncoderPtr = std::unique_ptr<GenericRequestEncoder>;
using GenericResponseEncoderPtr = std::unique_ptr<GenericResponseEncoder>;
using GenericMessageCreatorPtr = std::unique_ptr<GenericMessageCreator>;

/**
 * Factory used to create generic stream encoder and decoder. If the developer wants to add new
 * protocol support to this proxy, they need to implement the corresponding codec factory for the
 * corresponding protocol.
 */
class CodecFactory {
public:
  virtual ~CodecFactory() = default;

  /*
   * Create generic request decoder.
   */
  virtual GenericRequestDecoderPtr requestDecoder() const PURE;

  /*
   * Create generic response decoder.
   */
  virtual GenericResponseDecoderPtr responseDecoder() const PURE;

  /*
   * Create generic request encoder.
   */
  virtual GenericRequestEncoderPtr requestEncoder() const PURE;

  /*
   * Create generic response encoder.
   */
  virtual GenericResponseEncoderPtr responseEncoder() const PURE;

  /**
   * Create generic message creator.
   */
  virtual GenericMessageCreatorPtr messageCreator() const PURE;
};

using CodecFactoryPtr = std::unique_ptr<CodecFactory>;

/**
 * Factory config for codec factory. This class is used to register and create codec factories.
 */
class CodecFactoryConfig : public Envoy::Config::TypedFactory {
public:
  virtual CodecFactoryPtr createFactory(const Protobuf::Message& config,
                                        Envoy::Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "proxy.filters.network.generic_proxy.codec"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
