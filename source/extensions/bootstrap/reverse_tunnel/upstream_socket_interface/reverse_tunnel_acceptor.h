#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class ReverseTunnelAcceptorExtension;
class UpstreamSocketManager;

/**
 * Socket interface that creates upstream reverse connection sockets.
 * Manages cached reverse TCP connections and provides them when requested.
 */
class ReverseTunnelAcceptor : public Envoy::Network::SocketInterfaceBase,
                              public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
public:
  /**
   * Constructs a ReverseTunnelAcceptor with the given server factory context.
   *
   * @param context the server factory context for this socket interface.
   */
  ReverseTunnelAcceptor(Server::Configuration::ServerFactoryContext& context);

  ReverseTunnelAcceptor() : extension_(nullptr), context_(nullptr) {}

  // SocketInterface overrides
  /**
   * Create a socket without a specific address (no-op for reverse connections).
   * @param socket_type the type of socket to create.
   * @param addr_type the address type.
   * @param version the IP version.
   * @param socket_v6only whether to create IPv6-only socket.
   * @param options socket creation options.
   * @return nullptr since reverse connections require specific addresses.
   */
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type, Envoy::Network::Address::Type addr_type,
         Envoy::Network::Address::IpVersion version, bool socket_v6only,
         const Envoy::Network::SocketCreationOptions& options) const override;

  /**
   * Create a socket with a specific address.
   * @param socket_type the type of socket to create.
   * @param addr the address to bind to.
   * @param options socket creation options.
   * @return IoHandlePtr for the reverse connection socket.
   */
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type,
         const Envoy::Network::Address::InstanceConstSharedPtr addr,
         const Envoy::Network::SocketCreationOptions& options) const override;

  /**
   * @param domain the IP family domain (AF_INET, AF_INET6).
   * @return true if the family is supported.
   */
  bool ipFamilySupported(int domain) override;

  /**
   * @return pointer to the thread-local registry, or nullptr if not available.
   */
  class UpstreamSocketThreadLocal* getLocalRegistry() const;

  /**
   * Create a bootstrap extension for this socket interface.
   * @param config the config.
   * @param context the server factory context.
   * @return BootstrapExtensionPtr for the socket interface extension.
   */
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  /**
   * @return MessagePtr containing the empty configuration.
   */
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  /**
   * @return the interface name.
   */
  std::string name() const override {
    return "envoy.bootstrap.reverse_tunnel.upstream_socket_interface";
  }

  /**
   * @return pointer to the extension for cross-thread aggregation.
   */
  ReverseTunnelAcceptorExtension* getExtension() const { return extension_; }

  ReverseTunnelAcceptorExtension* extension_{nullptr};

private:
  Server::Configuration::ServerFactoryContext* context_;
};

DECLARE_FACTORY(ReverseTunnelAcceptor);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
