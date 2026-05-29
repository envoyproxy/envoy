#pragma once

#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Network {

/**
 * A connection handler that is balanced. Typically implemented by individual listeners depending
 * on their balancing configuration.
 */
class BalancedConnectionHandler {
public:
  virtual ~BalancedConnectionHandler() = default;

  /**
   * @return the number of active connections within the handler.
   */
  virtual uint64_t numConnections() const PURE;

  /**
   * Increment the connection count used for balancing decisions. Balancer implementations must call
   * this prior to returning from pickTargetHandler() to ensure accurate connection counts during
   * connection transfer. For balancers that hold a lock (e.g., ExactConnectionBalancerImpl), this
   * should be called inside the critical section, while postIncNumConnections() is called after
   * the lock is released.
   */
  virtual void preIncNumConnections() PURE;

  /**
   * Perform any resource-limit bookkeeping after a connection has been assigned by the balancer.
   * This is the counterpart to preIncNumConnections() and must be called outside any balancer lock
   * to avoid holding the lock longer than necessary.
   */
  virtual void postIncNumConnections() PURE;

  /**
   * Post a connected socket to this connection handler. This is used for cross-thread connection
   * transfer during the balancing process.
   */
  virtual void post(Network::ConnectionSocketPtr&& socket) PURE;

  /**
   * Main call to accept a socket on a worker.
   * @param hand_off_restored_destination_connections used to select the original destination
listener.
   * @param rebalanced indicates whether rebalancing is already done
   * @param network_namespace file path to the network namespace from the listener address
   */
  virtual void onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                              bool hand_off_restored_destination_connections, bool rebalanced,
                              const absl::optional<std::string>& network_namespace) PURE;
};

/**
 * An implementation of a connection balancer. This abstracts the underlying policy (e.g., exact,
 * fuzzy, etc.).
 */
class ConnectionBalancer {
public:
  virtual ~ConnectionBalancer() = default;

  /**
   * Register a new handler with the balancer that is available for balancing.
   */
  virtual void registerHandler(BalancedConnectionHandler& handler) PURE;

  /**
   * Unregister a handler with the balancer that is no longer available for balancing.
   */
  virtual void unregisterHandler(BalancedConnectionHandler& handler) PURE;

  /**
   * Pick a target handler to send a connection to.
   * @param current_handler supplies the currently executing connection handler.
   * @return current_handler if the connection should stay bound to the current handler, or a
   *         different handler if the connection should be rebalanced.
   *
   * NOTE: It is the responsibility of the balancer to call preIncNumConnections() and
   *       postIncNumConnections() on the returned handler. See the comments above for more
   *       explanation.
   */
  virtual BalancedConnectionHandler&
  pickTargetHandler(BalancedConnectionHandler& current_handler) PURE;
};

using ConnectionBalancerSharedPtr = std::shared_ptr<ConnectionBalancer>;
using BalancedConnectionHandlerOptRef =
    absl::optional<std::reference_wrapper<BalancedConnectionHandler>>;

} // namespace Network
} // namespace Envoy
