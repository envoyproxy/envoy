#pragma once

#include <cstdint>
#include <memory>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/ssl/context.h"

namespace Envoy {
namespace Network {

/**
 * Abstract connection handler.
 */
class ConnectionHandler {
public:
  virtual ~ConnectionHandler() = default;

  /**
   * @return uint64_t the number of active connections owned by the handler.
   */
  virtual uint64_t numConnections() const PURE;

  /**
   * Increment the return value of numConnections() by one.
   * TODO(mattklein123): re-visit the connection accounting interface. Make TCP
   * listener to do accounting through these interfaces instead of directly
   * access the counter.
   */
  virtual void incNumConnections() PURE;

  /**
   * Decrement the return value of numConnections() by one.
   */
  virtual void decNumConnections() PURE;

  /**
   * Adds a listener to the handler.
   * @param config listener configuration options.
   */
  virtual void addListener(ListenerConfig& config) PURE;

  /**
   * Remove listeners using the listener tag as a key. All connections owned by the removed
   * listeners will be closed.
   * @param listener_tag supplies the tag passed to addListener().
   */
  virtual void removeListeners(uint64_t listener_tag) PURE;

  /**
   * Stop listeners using the listener tag as a key. This will not close any connections and is used
   * for draining.
   * @param listener_tag supplies the tag passed to addListener().
   */
  virtual void stopListeners(uint64_t listener_tag) PURE;

  /**
   * Stop all listeners. This will not close any connections and is used for draining.
   */
  virtual void stopListeners() PURE;

  /**
   * Disable all listeners. This will not close any connections and is used to temporarily
   * stop accepting connections on all listeners.
   */
  virtual void disableListeners() PURE;

  /**
   * Enable all listeners. This is used to re-enable accepting connections on all listeners
   * after they have been temporarily disabled.
   */
  virtual void enableListeners() PURE;

  /**
   * @return the stat prefix used for per-handler stats.
   */
  virtual const std::string& statPrefix() const PURE;

  /**
   * Used by ConnectionHandler to manage listeners.
   */
  class ActiveListener {
  public:
    virtual ~ActiveListener() = default;

    /**
     * @return the tag value as configured.
     */
    virtual uint64_t listenerTag() PURE;

    /**
     * @return the actual Listener object.
     */
    virtual Listener* listener() PURE;

    /**
     * Temporarily stop listening according to implementation's own definition.
     */
    virtual void pauseListening() PURE;

    /**
     * Resume listening according to implementation's own definition.
     */
    virtual void resumeListening() PURE;

    /**
     * Stop listening according to implementation's own definition.
     */
    virtual void shutdownListener() PURE;
  };

  using ActiveListenerPtr = std::unique_ptr<ActiveListener>;
};

using ConnectionHandlerPtr = std::unique_ptr<ConnectionHandler>;

/**
 * A registered factory interface to create different kinds of ActiveUdpListener.
 */
class ActiveUdpListenerFactory {
public:
  virtual ~ActiveUdpListenerFactory() = default;

  /**
   * Creates an ActiveUdpListener object and a corresponding UdpListener
   * according to given config.
   * @param parent is the owner of the created ActiveListener objects.
   * @param dispatcher is used to create actual UDP listener.
   * @param config provides information needed to create ActiveUdpListener and
   * UdpListener objects.
   * @return the ActiveUdpListener created.
   */
  virtual ConnectionHandler::ActiveListenerPtr
  createActiveUdpListener(ConnectionHandler& parent, Event::Dispatcher& disptacher,
                          Network::ListenerConfig& config) PURE;

  /**
   * @return true if the UDP passing through listener doesn't form stateful connections.
   */
  virtual bool isTransportConnectionless() const PURE;
};

using ActiveUdpListenerFactoryPtr = std::unique_ptr<ActiveUdpListenerFactory>;

} // namespace Network
} // namespace Envoy
