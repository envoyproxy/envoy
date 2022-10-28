#pragma once

#include <cstdint>
#include <memory>

#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_balancer.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/runtime/runtime.h"
#include "envoy/ssl/context.h"

#include "source/common/common/interval_value.h"

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
   * Adds a listener to the handler, optionally replacing the existing listener.
   * @param overridden_listener tag of the existing listener. nullopt if no previous listener.
   * @param config listener configuration options.
   * @param runtime the runtime for the server.
   */
  virtual void addListener(absl::optional<uint64_t> overridden_listener, ListenerConfig& config,
                           Runtime::Loader& runtime) PURE;

  /**
   * Remove listeners using the listener tag as a key. All connections owned by the removed
   * listeners will be closed.
   * @param listener_tag supplies the tag passed to addListener().
   */
  virtual void removeListeners(uint64_t listener_tag) PURE;

  /**
   * Remove the filter chains and the connections in the listener. All connections owned
   * by the filter chains will be closed. Once all the connections are destroyed(connections
   * could be deferred deleted!), invoke the completion.
   * @param listener_tag supplies the tag passed to addListener().
   * @param filter_chains supplies the filter chains to be removed.
   */
  virtual void removeFilterChains(uint64_t listener_tag,
                                  const std::list<const FilterChain*>& filter_chains,
                                  std::function<void()> completion) PURE;

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
   * Set the fraction of connections the listeners should reject.
   * @param reject_fraction a value between 0 (reject none) and 1 (reject all).
   */
  virtual void setListenerRejectFraction(UnitFloat reject_fraction) PURE;

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

    /**
     * Update the listener config.
     */
    virtual void updateListenerConfig(Network::ListenerConfig& config) PURE;

    /**
     * Called when the given filter chains are about to be removed.
     */
    virtual void onFilterChainDraining(
        const std::list<const Network::FilterChain*>& draining_filter_chains) PURE;
  };

  using ActiveListenerPtr = std::unique_ptr<ActiveListener>;

  /**
   * Used by ConnectionHandler to manage UDP listeners.
   */
  class ActiveUdpListener : public virtual ActiveListener, public Network::UdpListenerCallbacks {
  public:
    ~ActiveUdpListener() override = default;

    /**
     * Returns the worker index that ``data`` should be delivered to. The return value must be in
     * the range [0, concurrency).
     */
    virtual uint32_t destination(const Network::UdpRecvData& data) const PURE;
  };

  using ActiveUdpListenerPtr = std::unique_ptr<ActiveUdpListener>;
};

using ConnectionHandlerPtr = std::unique_ptr<ConnectionHandler>;

/**
 * The connection handler from the view of a tcp listener.
 */
class TcpConnectionHandler : public virtual ConnectionHandler {
public:
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Obtain the rebalancer of the tcp listener.
   * @param listener_tag supplies the tag of the tcp listener that was passed to addListener().
   * @param address is used to query the address specific handler.
   * @return BalancedConnectionHandlerOptRef the balancer attached to the listener. `nullopt` if
   * listener doesn't exist or rebalancer doesn't exist.
   */
  virtual BalancedConnectionHandlerOptRef
  getBalancedHandlerByTag(uint64_t listener_tag, const Network::Address::Instance& address) PURE;

  /**
   * Obtain the rebalancer of the tcp listener.
   * @param address supplies the address of the tcp listener.
   * @return BalancedConnectionHandlerOptRef the balancer attached to the listener. ``nullopt`` if
   * listener doesn't exist or rebalancer doesn't exist.
   */
  virtual BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) PURE;
};

/**
 * The connection handler from the view of a udp listener.
 */
class UdpConnectionHandler : public virtual ConnectionHandler {
public:
  /**
   * Get the ``UdpListenerCallbacks`` associated with ``listener_tag`` and ``address``. This will be
   * absl::nullopt for non-UDP listeners and for ``listener_tag`` values that have already been
   * removed.
   */
  virtual UdpListenerCallbacksOptRef
  getUdpListenerCallbacks(uint64_t listener_tag, const Network::Address::Instance& address) PURE;
};

/**
 * A registered factory interface to create different kinds of ActiveUdpListener.
 */
class ActiveUdpListenerFactory {
public:
  virtual ~ActiveUdpListenerFactory() = default;

  /**
   * Creates an ActiveUdpListener object and a corresponding UdpListener
   * according to given config.
   * @param runtime the runtime for this server.
   * @param worker_index The index of the worker this listener is being created on.
   * @param parent is the owner of the created ActiveListener objects.
   * @param listen_socket_ptr is the UDP socket.
   * @param dispatcher is used to create actual UDP listener.
   * @param config provides information needed to create ActiveUdpListener and
   * UdpListener objects.
   * @return the ActiveUdpListener created.
   */
  virtual ConnectionHandler::ActiveUdpListenerPtr
  createActiveUdpListener(Runtime::Loader& runtime, uint32_t worker_index,
                          UdpConnectionHandler& parent,
                          Network::SocketSharedPtr&& listen_socket_ptr,
                          Event::Dispatcher& dispatcher, Network::ListenerConfig& config) PURE;

  /**
   * @return true if the UDP passing through listener doesn't form stateful connections.
   */
  virtual bool isTransportConnectionless() const PURE;

  /**
   * @return socket options specific to this factory that should be applied to all sockets.
   */
  virtual const Network::Socket::OptionsSharedPtr& socketOptions() const PURE;
};

using ActiveUdpListenerFactoryPtr = std::unique_ptr<ActiveUdpListenerFactory>;

/**
 * Internal listener callbacks.
 */
class InternalListener : public virtual ConnectionHandler::ActiveListener {
public:
  /**
   * Called when a new connection is accepted.
   * @param socket supplies the socket that is moved into the callee.
   */
  virtual void onAccept(ConnectionSocketPtr&& socket) PURE;
};

using InternalListenerPtr = std::unique_ptr<InternalListener>;
using InternalListenerOptRef = OptRef<InternalListener>;

/**
 * The query interface of the registered internal listener callbacks.
 */
class InternalListenerManager {
public:
  virtual ~InternalListenerManager() = default;

  /**
   * Return the internal listener binding the listener address.
   *
   * @param listen_address the internal address of the expected internal listener.
   */
  virtual InternalListenerOptRef
  findByAddress(const Address::InstanceConstSharedPtr& listen_address) PURE;
};

using InternalListenerManagerOptRef =
    absl::optional<std::reference_wrapper<InternalListenerManager>>;

// The thread local registry.
class LocalInternalListenerRegistry {
public:
  virtual ~LocalInternalListenerRegistry() = default;

  // Set the internal listener manager which maintains life of internal listeners. Called by
  // connection handler.
  virtual void setInternalListenerManager(InternalListenerManager& internal_listener_manager) PURE;

  // Get the internal listener manager to obtain a listener. Called by client connection factory.
  virtual InternalListenerManagerOptRef getInternalListenerManager() PURE;

  // Create a new active internal listener. Called by the server connection handler.
  virtual InternalListenerPtr createActiveInternalListener(ConnectionHandler& conn_handler,
                                                           ListenerConfig& config,
                                                           Event::Dispatcher& dispatcher) PURE;
};

// The central internal listener registry interface providing the thread local accessor.
class InternalListenerRegistry {
public:
  virtual ~InternalListenerRegistry() = default;

  /**
   * @return The thread local registry.
   */
  virtual LocalInternalListenerRegistry* getLocalRegistry() PURE;
};

} // namespace Network
} // namespace Envoy
