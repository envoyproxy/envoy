#pragma once

#include <string>

#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"

#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom IoHandle for upstream reverse connections that manages ConnectionSocket lifetime.
 * This class implements RAII principles to ensure proper socket cleanup and provides
 * reverse connection semantics where the connection is already established.
 */
class UpstreamReverseConnectionIOHandle : public Network::IoSocketHandleImpl {
public:
  /**
   * Constructs an UpstreamReverseConnectionIOHandle that takes ownership of a socket.
   *
   * @param socket the reverse connection socket to own and manage.
   * @param cluster_name the name of the cluster this connection belongs to.
   */
  UpstreamReverseConnectionIOHandle(Network::ConnectionSocketPtr socket,
                                    const std::string& cluster_name);

  ~UpstreamReverseConnectionIOHandle() override;

  // Network::IoHandle overrides
  /**
   * Override of connect method for reverse connections.
   * For reverse connections, the connection is already established so this method
   * is a no-op and always returns success.
   *
   * @param address the target address (unused for reverse connections).
   * @return SysCallIntResult with success status (0, 0).
   */
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;

  /**
   * Override of close method for reverse connections.
   * Cleans up the owned socket and calls the parent close method.
   *
   * @return IoCallUint64Result indicating the result of the close operation.
   */
  Api::IoCallUint64Result close() override;

  /**
   * Override of shutdown for reverse connections.
   * When the IO handle owns the socket, ignore shutdown to avoid affecting the handed-off socket.
   *
   * @param how the type of shutdown (`SHUT_RD`, `SHUT_WR`, `SHUT_RDWR`).
   * @return SysCallIntResult with success status if ignored, or result of base call.
   */
  Api::SysCallIntResult shutdown(int how) override;

  /**
   * Get the owned socket for read-only operations.
   *
   * @return const reference to the owned socket.
   */
  const Network::ConnectionSocket& getSocket() const { return *owned_socket_; }

private:
  // The name of the cluster this reverse connection belongs to.
  std::string cluster_name_;
  // The socket that this IOHandle owns and manages lifetime for.
  Network::ConnectionSocketPtr owned_socket_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
