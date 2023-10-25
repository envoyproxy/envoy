#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "contrib/generic_proxy/filters/network/source/interface/codec_callbacks.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Server codec that used to decode downstream request and encode upstream response.
 * This codec is used by downstream connection.
 */
class ServerCodec {
public:
  virtual ~ServerCodec() = default;

  /**
   * Set callbacks of server codec.
   * @param callbacks callbacks of server codec. This callback will have same lifetime
   * as the server codec.
   */
  virtual void setCodecCallbacks(ServerCodecCallbacks& callbacks) PURE;

  /**
   * Decode request frame from downstream connection.
   * @param buffer data to decode.
   * @param end_stream whether this is the last data of the downstream connection.
   */
  virtual void decode(Buffer::Instance& buffer, bool end_stream) PURE;

  /**
   * Encode response frame.
   * @param frame response frame to encode.
   * @param callbacks callbacks of encoding. This callback should be used to notify the
   * generic proxy filter that the response is encoded and should be called only once.
   */
  virtual void encode(const StreamFrame& frame, EncodingCallbacks& callbacks) PURE;

  /**
   * Create a response frame with specified status and flags.
   * @param status status of the response.
   * @param short_response_flags short flags of the response.
   * @param request origin request that the response is created for.
   */
  virtual ResponsePtr respond(Status status, absl::string_view short_response_flags,
                              const Request& request) PURE;
};

/**
 * Client codec that used to decode upstream response and encode downstream request.
 * This codec is used by upstream connection.
 */
class ClientCodec {
public:
  virtual ~ClientCodec() = default;

  /**
   * Set callbacks of client codec.
   * @param callbacks callbacks of client codec. This callback will have same lifetime
   * as the client codec.
   */
  virtual void setCodecCallbacks(ClientCodecCallbacks& callbacks) PURE;

  /**
   * Decode response frame from upstream connection.
   * @param buffer data to decode.
   * @param end_stream whether this is the last data of the upstream connection.
   */
  virtual void decode(Buffer::Instance& buffer, bool end_stream) PURE;

  /**
   * Encode request frame.
   * @param frame request frame to encode.
   * @param callbacks callbacks of encoding. This callbacks should be used to notify the
   * generic proxy filter that the request is encoded and should be called only once.
   */
  virtual void encode(const StreamFrame& frame, EncodingCallbacks& callbacks) PURE;
};

using ServerCodecPtr = std::unique_ptr<ServerCodec>;
using ClientCodecPtr = std::unique_ptr<ClientCodec>;

/**
 * Factory used to create generic stream encoder and decoder. If the developer wants to add
 * new protocol support to this proxy, they need to implement the corresponding codec factory for
 * the corresponding protocol.
 */
class CodecFactory {
public:
  virtual ~CodecFactory() = default;

  /**
   * Create a server codec instance.
   */
  virtual ServerCodecPtr createServerCodec() const PURE;

  /**
   * Create a client codec instance.
   */
  virtual ClientCodecPtr createClientCodec() const PURE;
};

using CodecFactoryPtr = std::unique_ptr<CodecFactory>;

class FilterConfig;
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Custom read filter factory for generic proxy.
 */
class ProxyFactory {
public:
  virtual ~ProxyFactory() = default;

  /**
   * Create a custom proxy instance.
   * @param filter_manager the filter manager of the network filter chain.
   * @param filter_config supplies the read filter config.
   */
  virtual void createProxy(Network::FilterManager& filter_manager,
                           const FilterConfigSharedPtr& filter_config) const PURE;
};
using ProxyFactoryPtr = std::unique_ptr<ProxyFactory>;

/**
 * Factory config for codec factory. This class is used to register and create codec factories.
 */
class CodecFactoryConfig : public Envoy::Config::TypedFactory {
public:
  /**
   * Create a codec factory. This should never return nullptr.
   * @param config supplies the config.
   * @param context supplies the server context.
   * @return CodecFactoryPtr the codec factory.
   */
  virtual CodecFactoryPtr createCodecFactory(const Protobuf::Message&,
                                             Envoy::Server::Configuration::FactoryContext&) PURE;

  /**
   * Create a optional custom proxy factory.
   * @param config supplies the config.
   * @param context supplies the server context.
   * @return ProxyFactoryPtr the proxy factory to create generic proxy instance or nullptr if no
   * custom proxy is needed and the default generic proxy will be used.
   */
  virtual ProxyFactoryPtr createProxyFactory(const Protobuf::Message&,
                                             Envoy::Server::Configuration::FactoryContext&) {
    return nullptr;
  }

  std::string category() const override { return "envoy.generic_proxy.codecs"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
