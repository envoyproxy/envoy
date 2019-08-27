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
  virtual uint64_t numConnections() PURE;

  /**
   * Adds a listener to the handler.
   * @param config listener configuration options.
   */
  virtual void addListener(ListenerConfig& config) PURE;

  /**
   * Find a listener based on the provided listener address value.
   * @param address supplies the address value.
   * @return a pointer to the listener or nullptr if not found.
   * Ownership of the listener is NOT transferred
   */
  virtual Network::Listener* findListenerByAddress(const Network::Address::Instance& address) PURE;

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
     * Destroy the actual Listener it wraps.
     */
    virtual void destroy() PURE;
  };

  using ActiveListenerPtr = std::unique_ptr<ActiveListener>;
};

using ConnectionHandlerPtr = std::unique_ptr<ConnectionHandler>;

/**
 * A registered factory interface to create different kinds of
 * ActiveUdpListener.
 */
class ActiveUdpListenerFactory {
public:
  virtual ~ActiveUdpListenerFactory() = default;

  /**
   * Creates an ActiveUdpListener object and a corresponding UdpListener
   * according to given config.
   * @param parent is the owner of the created ActiveListener objects.
   * @param dispatcher is used to create actual UDP listener.
   * @param logger might not need to be passed in.
   * TODO(danzh): investigate if possible to use statically defined logger in ActiveUdpListener
   * implementation instead.
   * @param config provides information needed to create ActiveUdpListener and
   * UdpListener objects.
   * @return the ActiveUdpListener created.
   */
  virtual ConnectionHandler::ActiveListenerPtr
  createActiveUdpListener(ConnectionHandler& parent, Event::Dispatcher& disptacher,
                          spdlog::logger& logger, Network::ListenerConfig& config) const PURE;
};

using ActiveUdpListenerFactoryPtr = std::unique_ptr<ActiveUdpListenerFactory>;

} // namespace Network
} // namespace Envoy
