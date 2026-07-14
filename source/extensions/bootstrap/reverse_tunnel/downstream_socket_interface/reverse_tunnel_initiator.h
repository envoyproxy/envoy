#pragma once

#include <memory>
#include <string>

#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_address.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
struct ReverseConnectionSocketConfig;

/**
 * Socket interface that creates reverse connection sockets.
 * This class implements the SocketInterface interface to provide reverse connection
 * functionality for downstream connections.
 */
class ReverseTunnelInitiator : public Envoy::Network::SocketInterfaceBase,
                               public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
  // Friend class for testing
  friend class ReverseTunnelInitiatorTest;

public:
  ReverseTunnelInitiator(Server::Configuration::ServerFactoryContext& context);

  // Default constructor for registry
  ReverseTunnelInitiator() : extension_(nullptr), context_(nullptr) {}

  /**
   * Create a ReverseConnectionIOHandle and kick off the reverse connection establishment.
   * @param socket_type the type of socket to create
   * @param addr_type the address type
   * @param version the IP version
   * @param socket_v6only whether to create IPv6-only socket
   * @param options socket creation options
   * @return IoHandlePtr for the created socket, or nullptr for unsupported types
   */
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type, Envoy::Network::Address::Type addr_type,
         Envoy::Network::Address::IpVersion version, bool socket_v6only,
         const Envoy::Network::SocketCreationOptions& options) const override;

  // No-op for reverse connections.
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type,
         const Envoy::Network::Address::InstanceConstSharedPtr addr,
         const Envoy::Network::SocketCreationOptions& options) const override;

  /**
   * @return true if the IP family is supported
   */
  bool ipFamilySupported(int domain) override;

  /**
   * @return pointer to the thread-local registry, or nullptr if not available.
   */
  DownstreamSocketThreadLocal* getLocalRegistry() const;

  /**
   * Thread-safe helper method to create reverse connection socket with config.
   * @param socket_type the type of socket to create
   * @param addr_type the address type
   * @param version the IP version
   * @param config the reverse connection configuration
   * @return IoHandlePtr for the reverse connection socket
   */
  Envoy::Network::IoHandlePtr
  createReverseConnectionSocket(Envoy::Network::Socket::Type socket_type,
                                Envoy::Network::Address::Type addr_type,
                                Envoy::Network::Address::IpVersion version,
                                const ReverseConnectionSocketConfig& config) const;

  /**
   * Get the extension instance for accessing cross-thread aggregation capabilities.
   * @return pointer to the extension, or nullptr if not available
   */
  ReverseTunnelInitiatorExtension* getExtension() const { return extension_; }

  // BootstrapExtensionFactory implementation
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override {
    return "envoy.bootstrap.reverse_tunnel.downstream_socket_interface";
  }

  ReverseTunnelInitiatorExtension* extension_;

private:
  Server::Configuration::ServerFactoryContext* context_;
};

DECLARE_FACTORY(ReverseTunnelInitiator);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
