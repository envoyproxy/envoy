#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/io_error.h"
#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_balancer.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Network {

class ActiveUdpListenerFactory;

/**
 * A configuration for an individual listener.
 */
class ListenerConfig {
public:
  virtual ~ListenerConfig() = default;

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
   *         connection being created unless continueOnListenerFiltersTimeout() returns true.
   *         0 specifies a disabled timeout.
   */
  virtual std::chrono::milliseconds listenerFiltersTimeout() const PURE;

  /**
   * @return bool whether the listener should try to create a connection when listener filters
   *         time out.
   */
  virtual bool continueOnListenerFiltersTimeout() const PURE;

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

  /**
   * @return factory pointer if listening on UDP socket, otherwise return
   * nullptr.
   */
  virtual const ActiveUdpListenerFactory* udpListenerFactory() PURE;

  /**
   * @return traffic direction of the listener.
   */
  virtual envoy::api::v2::core::TrafficDirection direction() const PURE;

  /**
   * @return the connection balancer for this listener. All listeners have a connection balancer,
   *         though the implementation may be a NOP balancer.
   */
  virtual ConnectionBalancer& connectionBalancer() PURE;
};

/**
 * Callbacks invoked by a listener.
 */
class ListenerCallbacks {
public:
  virtual ~ListenerCallbacks() = default;

  /**
   * Called when a new connection is accepted.
   * @param socket supplies the socket that is moved into the callee.
   */
  virtual void onAccept(ConnectionSocketPtr&& socket) PURE;
};

/**
 * Utility struct that encapsulates the information from a udp socket's
 * recvfrom/recvmmsg call.
 *
 * TODO(conqerAtapple): Maybe this belongs inside the UdpListenerCallbacks
 * class.
 */
struct UdpRecvData {
  Address::InstanceConstSharedPtr local_address_;
  Address::InstanceConstSharedPtr peer_address_; // TODO(conquerAtapple): Fix ownership semantics.
  Buffer::InstancePtr buffer_;
  MonotonicTime receive_time_;

  // TODO(conquerAtapple):
  // Add UdpReader here so that the callback handler can
  // then use the reader to do multiple reads(recvmmsg) once the OS notifies it
  // has data. We could also just return a `ReaderFactory` that returns either a
  // `recvfrom` reader (with peer information) or a `read/recvmmsg` reader. This
  // is still being flushed out (Jan, 2019).
};

/**
 * Encapsulates the information needed to send a udp packet to a target
 */
struct UdpSendData {
  const Address::Ip* local_ip_;
  const Address::Instance& peer_address_;

  // The buffer is a reference so that it can be reused by the sender to send different
  // messages
  Buffer::Instance& buffer_;
};

/**
 * UDP listener callbacks.
 */
class UdpListenerCallbacks {
public:
  enum class ErrorCode { SyscallError, UnknownError };

  virtual ~UdpListenerCallbacks() = default;

  /**
   * Called whenever data is received by the underlying udp socket.
   *
   * @param data UdpRecvData from the underlying socket.
   */
  virtual void onData(UdpRecvData& data) PURE;

  /**
   * Called when the underlying socket is ready for write.
   *
   * @param socket Underlying server socket for the listener.
   *
   * TODO(conqerAtapple): Maybe we need a UdpWriter here instead of Socket.
   */
  virtual void onWriteReady(const Socket& socket) PURE;

  /**
   * Called when there is an error event in the receive data path.
   * The send side error is a return type on the send method.
   *
   * @param error_code ErrorCode for the error event.
   * @param error_number System error number.
   */
  virtual void onReceiveError(const ErrorCode& error_code, Api::IoError::IoErrorCode err) PURE;
};

/**
 * An abstract socket listener. Free the listener to stop listening on the socket.
 */
class Listener {
public:
  virtual ~Listener() = default;

  /**
   * Temporarily disable accepting new connections.
   */
  virtual void disable() PURE;

  /**
   * Enable accepting new connections.
   */
  virtual void enable() PURE;
};

using ListenerPtr = std::unique_ptr<Listener>;

/**
 * A UDP listener interface.
 */
class UdpListener : public virtual Listener {
public:
  ~UdpListener() override = default;

  /**
   * @return Event::Dispatcher& the dispatcher backing this listener.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return the local address of the socket.
   */
  virtual const Network::Address::InstanceConstSharedPtr& localAddress() const PURE;

  /**
   * Send data through the underlying udp socket. If the send buffer of the socket FD is full, an
   * error code is returned.
   * TODO(sumukhs): We do not currently handle max MTU size of the datagram. Determine if we could
   * expose the path MTU information to the caller.
   *
   * @param data Supplies the data to send to a target using udp.
   * @return the error code of the underlying send api. On successfully sending 'n' bytes, the
   * underlying buffers in the data  are drained by 'n' bytes. The remaining can be retried by the
   * sender.
   */
  virtual Api::IoCallUint64Result send(const UdpSendData& data) PURE;
};

using UdpListenerPtr = std::unique_ptr<UdpListener>;

/**
 * Thrown when there is a runtime error creating/binding a listener.
 */
class CreateListenerException : public EnvoyException {
public:
  CreateListenerException(const std::string& what) : EnvoyException(what) {}
};

} // namespace Network
} // namespace Envoy
