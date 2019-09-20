#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/io_error.h"
#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Network {

class UdpListenerFilterManager;
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
   * @param hand_off_restored_destination_connections is true when the socket was first accepted by
   * another listener and is redirected to a new listener. The recipient should not redirect the
   * socket any further.
   */
  virtual void onAccept(ConnectionSocketPtr&& socket,
                        bool hand_off_restored_destination_connections = true) PURE;
