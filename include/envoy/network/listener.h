#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"

namespace Envoy {
namespace Network {

/**
 * A configuration for an individual listener.
 */
class ListenerConfig {
public:
  virtual ~ListenerConfig() {}

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

  /**
   * @return TransportSocketFactory& the transport socket factory.
   */
  virtual TransportSocketFactory& transportSocketFactory() PURE;

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
  virtual uint32_t perConnectionBufferLimitBytes() PURE;

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
   * @param redirected is true when the socket was first accepted by another listener
   * and is redirected to a new listener. The recipient should not redirect
   * the socket any further.
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
 * An abstract socket listener. Free the listener to stop listening on the socket.
 */
class Listener {
public:
  virtual ~Listener() {}
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
