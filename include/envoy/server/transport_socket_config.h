#pragma once

#include <string>

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context_manager.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Context passed to transport socket factory to access server resources.
 */
class TransportSocketFactoryContext {
public:
  virtual ~TransportSocketFactoryContext() {}

  /**
   * @return Ssl::ContextManager& the SSL context manager
   */
  virtual Ssl::ContextManager& sslContextManager() PURE;

  /**
   * @return Stats::Scope& the transport socket's stats scope.
   */
  virtual Stats::Scope& statsScope() const PURE;
};

class TransportSocketConfigFactory {
public:
  virtual ~TransportSocketConfigFactory() {}

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message. The transport socket
   *         config, which arrives in an opaque google.protobuf.Struct message, will be converted
   *         to JSON and then parsed into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular TransportSocketFactoryPtr
   *         implementation produced by the factory.
   */
  virtual std::string name() const PURE;
};

/**
 * Implemented by each transport socket and registered via class RegisterFactory for upstream
 * transport sockets.
 */
class UpstreamTransportSocketConfigFactory : public virtual TransportSocketConfigFactory {
public:
  /**
   * Create a particular transport socket factory implementation.
   * @param config const Protobuf::Message& supplies the config message for the transport socket
   *        implementation.
   * @param context TransportSocketFactoryContext& supplies the transport socket's context.
   * @return Network::TransportSocketFactoryPtr the transport socket factory instance. The returned
   *         TransportSocketFactoryPtr should not be nullptr.
   *
   * @throw EnvoyException if the implementation is unable to produce a factory with the provided
   *        parameters.
   */
  virtual Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               TransportSocketFactoryContext& context) PURE;
};

/**
 * Implemented by each transport socket and registered via class RegisterFactory for downstream
 * transport sockets.
 */
class DownstreamTransportSocketConfigFactory : public virtual TransportSocketConfigFactory {
public:
  /**
   * Create a particular downstream transport socket factory implementation.
   * TODO(lizan): Revisit the interface when TLS sniffing and filter chain match are implemented.
   * @param listener_name const std::string& the name of the listener.
   * @param server_names const std::vector<std::string>& the names of the server.
   * @param skip_ssl_context_update bool indicates whether the ssl context update should be skipped.
   * @param config const Protobuf::Message& supplies the config message for the transport socket
   *        implementation.
   * @param context TransportSocketFactoryContext& supplies the transport socket's context.
   * @return Network::TransportSocketFactoryPtr the transport socket factory instance. The returned
   *         TransportSocketFactoryPtr should not be nullptr.
   *
   * @throw EnvoyException if the implementation is unable to produce a factory with the provided
   *        parameters.
   */
  virtual Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const std::string& listener_name,
                               const std::vector<std::string>& server_names,
                               bool skip_ssl_context_update, const Protobuf::Message& config,
                               TransportSocketFactoryContext& context) PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
