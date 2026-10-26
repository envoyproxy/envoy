#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
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
   * the originating connection object is already gone). ``residual_bytes`` holds any bytes the
   * responder coalesced with the handshake response, to serve before the socket, and is null when
   * nothing was coalesced.
   */
  DownstreamReverseConnectionIOHandle(Network::ConnectionSocketPtr socket,
                                      ReverseConnectionIOHandle* parent,
                                      const std::string& connection_key, uint64_t connection_id,
                                      Buffer::InstancePtr residual_bytes = nullptr);

  ~DownstreamReverseConnectionIOHandle() override;

  // Network::IoHandle overrides.
  // Serve handshake residual bytes before the socket, then defer to the RPING interceptor.
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               std::optional<uint64_t> max_length) override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
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
  // Bytes the responder coalesced with the handshake response, served before the socket. Null once
  // nothing is left to replay.
  Buffer::InstancePtr residual_bytes_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
