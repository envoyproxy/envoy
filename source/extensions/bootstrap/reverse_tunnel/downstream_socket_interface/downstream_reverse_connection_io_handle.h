#pragma once

#include <string>

#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_handle_impl.h"

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
class DownstreamReverseConnectionIOHandle : public Network::IoSocketHandleImpl {
public:
  /**
   * Constructor that takes ownership of the socket and stores parent pointer and connection key.
   */
  DownstreamReverseConnectionIOHandle(Network::ConnectionSocketPtr socket,
                                      ReverseConnectionIOHandle* parent,
                                      const std::string& connection_key);

  ~DownstreamReverseConnectionIOHandle() override;

  // Network::IoHandle overrides.
  // Intercept reads to handle reverse connection keep-alive pings.
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;
  Api::IoCallUint64Result close() override;
  Api::SysCallIntResult shutdown(int how) override;

  /**
   * Tell this IO handle to ignore close() and shutdown() calls.
   * This is called by the HTTP filter during socket hand-off to prevent
   * the handed-off socket from being affected by connection cleanup.
   */
  void ignoreCloseAndShutdown() { ignore_close_and_shutdown_ = true; }

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
  // Flag to ignore close and shutdown calls during socket hand-off.
  bool ignore_close_and_shutdown_{false};

  // Whether to actively echo RPING messages while the connection is idle.
  // Disabled permanently after the first non-RPING application byte is observed.
  bool ping_echo_active_{true};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
