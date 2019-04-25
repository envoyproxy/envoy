#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Network {

/**
 * A configuration for an individual listener.
 */
class ListenerConfig {
public:
  virtual ~ListenerConfig() {}

  /**
   * @return FilterChainManager& the factory for adding and searching through configured
   *         filter chains.
   */
  virtual FilterChainManager& filterChainManager() PURE;

  /**
   * @return FilterChainFactory& the factory for setting up the filter chain on a new
   *         connection.
   */
  virtual FilterChainFactory& filterChainFactory() PURE;

  /**
   * @return Socket& the actual listen socket. The address of this socket may be
   *         different from configured if for example the configured address binds to port zero.
   */
  virtual Socket& socket() PURE;
  virtual const Socket& socket() const PURE;

  /**
   * @return bool specifies whether the listener should actually listen on the port.
   *         A listener that doesn't listen on a port can only receive connections
   *         redirected from other listeners.
   */
  virtual bool bindToPort() PURE;

  /**
   * @return bool if a connection should be handed off to another Listener after the original
   *         destination address has been restored. 'true' when 'use_original_dst' flag in listener
   *         configuration is set, false otherwise. Note that this flag is deprecated and will be
   *         removed from the v2 API.
   */
  virtual bool handOffRestoredDestinationConnections() const PURE;

  /**
   * @return uint32_t providing a soft limit on size of the listener's new connection read and write
   *         buffers.
   */
  virtual uint32_t perConnectionBufferLimitBytes() const PURE;

  /**
   * @return std::chrono::milliseconds the time to wait for all listener filters to complete
   *         operation. If the timeout is reached, the accepted socket is closed without a
   *         connection being created. 0 specifies a disabled timeout.
   */
  virtual std::chrono::milliseconds listenerFiltersTimeout() const PURE;

  /**
   * @return Stats::Scope& the stats scope to use for all listener specific stats.
   */
  virtual Stats::Scope& listenerScope() PURE;

  /**
   * @return uint64_t the tag the listener should use for connection handler tracking.
   */
  virtual uint64_t listenerTag() const PURE;

  /**
   * @return const std::string& the listener's name.
   */
  virtual const std::string& name() const PURE;
};

/**
 * Callbacks invoked by a listener.
 */
class ListenerCallbacks {
public:
  virtual ~ListenerCallbacks() {}

  /**
   * Called when a new connection is accepted.
   * @param socket supplies the socket that is moved into the callee.
   * @param hand_off_restored_destination_connections is true when the socket was first accepted by
   * another listener and is redirected to a new listener. The recipient should not redirect the
   * socket any further.
   */
  virtual void onAccept(ConnectionSocketPtr&& socket,
                        bool hand_off_restored_destination_connections = true) PURE;

  /**
   * Called when a new connection is accepted.
   * @param new_connection supplies the new connection that is moved into the callee.
   */
  virtual void onNewConnection(ConnectionPtr&& new_connection) PURE;
};

/**
 * Utility struct that encapsulates the information from a udp socket's
 * recvfrom/recvmmsg call.
 *
 * TODO(conqerAtapple): Maybe this belongs inside the UdpListenerCallbacks
 * class.
 */
struct UdpData {
  Address::InstanceConstSharedPtr local_address_;
  Address::InstanceConstSharedPtr peer_address_; // TODO(conquerAtapple): Fix ownership semantics.
  Buffer::InstancePtr buffer_;
  // TODO(conquerAtapple):
  // Add UdpReader here so that the callback handler can
  // then use the reader to do multiple reads(recvmmsg) once the OS notifies it
  // has data. We could also just return a `ReaderFactory` that returns either a
  // `recvfrom` reader (with peer information) or a `read/recvmmsg` reader. This
  // is still being flushed out (Jan, 2019).
};

/**
 * Udp listener callbacks.
 */
class UdpListenerCallbacks {
public:
  enum class ErrorCode { SyscallError, UnknownError };

  virtual ~UdpListenerCallbacks() = default;

  /**
   * Called whenever data is received by the underlying udp socket.
   *
   * @param data UdpData from the underlying socket.
   */
  virtual void onData(const UdpData& data) PURE;

  /**
   * Called when the underlying socket is ready for write.
   *
   * @param socket Underlying server socket for the listener.
   *
   * TODO(conqerAtapple): Maybe we need a UdpWriter here instead of Socket.
   */
  virtual void onWriteReady(const Socket& socket) PURE;

  /**
   * Called when there is an error event.
   *
   * @param error_code ErrorCode for the error event.
   * @param error_number System error number.
   */
  virtual void onError(const ErrorCode& error_code, int error_number) PURE;
};

/**
 * An abstract socket listener. Free the listener to stop listening on the socket.
 */
class Listener {
public:
  virtual ~Listener() {}

  /**
   * Temporarily disable accepting new connections.
   */
  virtual void disable() PURE;

  /**
   * Enable accepting new connections.
   */
  virtual void enable() PURE;
};

typedef std::unique_ptr<Listener> ListenerPtr;

/**
 * Thrown when there is a runtime error creating/binding a listener.
 */
class CreateListenerException : public EnvoyException {
public:
  CreateListenerException(const std::string& what) : EnvoyException(what) {}
};

} // namespace Network
} // namespace Envoy
