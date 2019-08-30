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
   * balancer implementation prior to a connection being rebalanced to another handler. This makes
   * sure that connection counts are accurate during connection transfer (i.e., that the target
   * balancer accounts for the incoming connection).
   */
  virtual void incNumConnections() PURE;

  /**
   * Post a connected socket to this connection handler. This is used for cross-thread connection
   * transfer during the balancing process.
   */
  virtual void post(Network::ConnectionSocketPtr&& socket) PURE;
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
   * Determine whether a connection should be rebalanced or not.
   * @param socket supplies the socket that is eligible for balancing.
   * @param current_handler supplies the currently executing connection handler.
   * @return Rebalanced if the socket has been moved to another connection handler or Continue if
   *         the socket should be processed on the currently executing connection handler.
   */
  enum class BalanceConnectionResult { Rebalanced, Continue };
  virtual BalanceConnectionResult
  balanceConnection(Network::ConnectionSocketPtr&& socket,
                    BalancedConnectionHandler& current_handler) PURE;
};

using ConnectionBalancerPtr = std::unique_ptr<ConnectionBalancer>;

} // namespace Network
} // namespace Envoy
