#pragma once

#include <string>

#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/rping_interceptor.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declaration.
class ReverseConnectionIOHandle;

/**
 * Custom IoHandle for downstream reverse connections that owns a ConnectionSocket.
 * This class is used internally by ReverseConnectionIOHandle to manage the lifecycle
 * of accepted downstream connections.
 */
class DownstreamReverseConnectionIOHandle : public RpingInterceptor {
public:
  /**
   * Constructor that takes ownership of the socket and stores parent pointer and connection key.
   */
  DownstreamReverseConnectionIOHandle(Network::ConnectionSocketPtr socket,
                                      ReverseConnectionIOHandle* parent,
                                      const std::string& connection_key);

  ~DownstreamReverseConnectionIOHandle() override;

  // Network::IoHandle overrides.
  Api::IoCallUint64Result close() override;
  Api::SysCallIntResult shutdown(int how) override;

  // RPING Interceptor overrides.
  // Send the RPING response from here.
  void onPingMessage() override;

  /**
   * Get the owned socket for read-only access.
   */
  const Network::ConnectionSocket& getSocket() const { return *owned_socket_; }

private:
  // The socket that this IOHandle owns and manages lifetime for.
  Network::ConnectionSocketPtr owned_socket_;
  // Pointer to parent ReverseConnectionIOHandle for connection lifecycle management.
  ReverseConnectionIOHandle* parent_;
  // Connection key for tracking this specific connection.
  std::string connection_key_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
