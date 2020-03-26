#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/api/io_error.h"
#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_balancer.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Network {

class ActiveUdpListenerFactory;

/**
 * ListenSocketFactory is a member of ListenConfig to provide listen socket.
 * Listeners created from the same ListenConfig instance have listening sockets
 * provided by the same ListenSocketFactory instance.
 */
class ListenSocketFactory {
public:
  virtual ~ListenSocketFactory() = default;

  /**
   * Called during actual listener creation.
   * @return the socket to be used for a certain listener, which might be shared
   * with other listeners of the same config on other worker threads.
   */
  virtual SocketSharedPtr getListenSocket() PURE;

  /**
   * @return the type of the socket getListenSocket() returns.
   */
  virtual Address::SocketType socketType() const PURE;

  /**
   * @return the listening address of the socket getListenSocket() returns. Before getListenSocket()
   * is called, the return value might has 0 as port number if the config doesn't specify it.
   */
  virtual const Address::InstanceConstSharedPtr& localAddress() const PURE;

  /**
   * @return the socket shared by worker threads if any; otherwise return null.
   */
  virtual SocketOptRef sharedSocket() const PURE;
};

using ListenSocketFactorySharedPtr = std::shared_ptr<ListenSocketFactory>;

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
   * @return ListenSocketFactory& the factory to create listen socket.
   */
  virtual ListenSocketFactory& listenSocketFactory() PURE;

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
   * @return uint32_t providing a soft limit on size of the listener's new connection read and
   * write buffers.
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
  virtual ActiveUdpListenerFactory* udpListenerFactory() PURE;

  /**
   * @return traffic direction of the listener.
   */
  virtual envoy::config::core::v3::TrafficDirection direction() const PURE;

  /**
   * @return the connection balancer for this listener. All listeners have a connection balancer,
   *         though the implementation may be a NOP balancer.
   */
  virtual ConnectionBalancer& connectionBalancer() PURE;

  /**
   * @return std::vector<AccessLog::InstanceSharedPtr> access logs emitted by the listener.
   */
  virtual const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const PURE;
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
 * Utility struct that encapsulates the information from a udp socket's recvmmsg call.
 */
struct UdpRecvData {
  struct LocalPeerAddresses {
    bool operator==(const LocalPeerAddresses& rhs) const {
      // TODO(mattklein123): Implement a hash directly on Address that does not use strings.
      return local_->asStringView() == rhs.local_->asStringView() &&
             peer_->asStringView() == rhs.peer_->asStringView();
    }

    template <typename H> friend H AbslHashValue(H h, const LocalPeerAddresses& addresses) {
      // TODO(mattklein123): Implement a hash directly on Address that does not use strings.
      return H::combine(std::move(h), addresses.local_->asStringView(),
                        addresses.peer_->asStringView());
    }

    Address::InstanceConstSharedPtr local_;
    Address::InstanceConstSharedPtr peer_;
  };

  LocalPeerAddresses addresses_;
  Buffer::InstancePtr buffer_;
  MonotonicTime receive_time_;
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
  virtual ~UdpListenerCallbacks() = default;

  /**
   * Called whenever data is received by the underlying udp socket.
   * TODO(danzh2010): Consider returning a value to indicate if more work is to
   * be done in the next event loop due to a limit on how much processing is
   * allowed in each event loop.
   *
   * @param data UdpRecvData from the underlying socket.
   */
  virtual void onData(UdpRecvData& data) PURE;

  /**
   * Called when the underlying socket is ready for read, before onData() is
   * called. Called only once per event loop, even if followed by multiple
   * onData() calls.
   *
   */
  virtual void onReadReady() PURE;

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
   * @param error_code supplies the received error on the listener.
   */
  virtual void onReceiveError(Api::IoError::IoErrorCode error_code) PURE;
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
