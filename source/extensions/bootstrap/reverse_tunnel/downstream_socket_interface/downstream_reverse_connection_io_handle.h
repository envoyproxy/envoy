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

  /**
   * Key the parent IOHandle uses to track this tunnel (the local address of the outbound TCP
   * socket at handoff time). The drain-aware HCM passes this back to parent() when the tunnel
   * begins draining so the parent can drop it from tracking and dial a replacement.
   */
  const std::string& connectionKey() const { return connection_key_; }

  /**
   * Parent ReverseConnectionIOHandle that owns this tunnel, or nullptr if the parent has already
   * been torn down (it clears this back-pointer via detachParent() on teardown). Always re-check
   * for nullptr at the point of use rather than caching the result.
   */
  ReverseConnectionIOHandle* parent() const { return parent_; }

  /**
   * Called by the parent ReverseConnectionIOHandle when it is destroyed, so a surviving tunnel's
   * parent() returns nullptr instead of a dangling pointer.
   */
  void detachParent() { parent_ = nullptr; }

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
