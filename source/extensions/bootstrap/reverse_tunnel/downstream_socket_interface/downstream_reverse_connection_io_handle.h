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
   * Constructor that takes ownership of the socket and stores parent pointer, connection key, and
   * the initiator's per-connection identifier (retained so it can be reported at close time, when
   * the originating connection object is already gone).
   */
  DownstreamReverseConnectionIOHandle(Network::ConnectionSocketPtr socket,
                                      ReverseConnectionIOHandle* parent,
                                      const std::string& connection_key, uint64_t connection_id);

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
   * socket at handoff time). Passed to the parent on drain/close so it can drop the tunnel from
   * tracking and dial a replacement.
   */
  const std::string& connectionKey() const { return connection_key_; }

  /**
   * Called by the parent ReverseConnectionIOHandle when it is destroyed, so a surviving tunnel
   * clears its back-pointer instead of retaining a dangling parent pointer.
   */
  void detachParent() { parent_ = nullptr; }

  /**
   * Notify the parent that this tunnel has begun draining so it can drop the key from tracking
   * and dial a replacement. No-op if the parent has already been torn down (detachParent).
   * Forwards connection_key_ and connection_id_ so the parent's access log can correlate the
   * drain event with the later connection_closed event.
   */
  void markTunnelDrainingAndDialReplacement();

private:
  // The socket that this IOHandle owns and manages lifetime for.
  Network::ConnectionSocketPtr owned_socket_;
  // Pointer to parent ReverseConnectionIOHandle for connection lifecycle management.
  ReverseConnectionIOHandle* parent_;
  // Connection key for tracking this specific connection.
  std::string connection_key_;
  // The initiator's per-connection identifier, reported to the parent on close.
  uint64_t connection_id_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
