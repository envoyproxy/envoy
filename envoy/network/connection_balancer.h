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
   * Increment the number of connections within the handler. This must be called by a connection
   * balancer implementation prior to a connection being picked via pickTargetHandler(). This makes
   * sure that connection counts are accurate during connection transfer (i.e., that the target
   * balancer accounts for the incoming connection). This is done by the balancer vs. the
   * connection handler to account for different locking needs inside the balancer.
   */
  virtual void incNumConnections() PURE;

  /**
   * Post a connected socket to this connection handler. This is used for cross-thread connection
   * transfer during the balancing process.
   */
  virtual void post(Network::ConnectionSocketPtr&& socket) PURE;

  virtual void onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                              bool hand_off_restored_destination_connections, bool rebalanced) PURE;
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
   * NOTE: It is the responsibility of the balancer to call incNumConnections() on the returned
   *       balancer. See the comments above for more explanation.
   */
  virtual BalancedConnectionHandler&
  pickTargetHandler(BalancedConnectionHandler& current_handler) PURE;
};

using ConnectionBalancerSharedPtr = std::shared_ptr<ConnectionBalancer>;
using BalancedConnectionHandlerOptRef =
    absl::optional<std::reference_wrapper<BalancedConnectionHandler>>;

} // namespace Network
} // namespace Envoy
