#pragma once

#include <string>

#include "envoy/network/connection.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Network {

/**
 * Interface for a reverse connection manager. This is ancillary to the dispatcher and facilitates
 * creation and management of reverse connections It manages the lifecycle of several reverse
 * connection initiators, and provides data structures for storing and re-using connection sockets.
 */
class ReverseConnectionManager {
public:
  virtual ~ReverseConnectionManager() = default;

  // Returns the dispatcher that created this ReverseConnectionManager.
  virtual Event::Dispatcher& dispatcher() const PURE;

  /**
   * Sets the stats scope for logging initiated reverse connections with the local
   * envoy as initiator.
   * @param scope the base scope to be used.
   * @return the parent scope for RCManager stats.
   */
  virtual void initializeStats(Stats::Scope& scope) PURE;

  /**
   * Register a reverse connection creation request with the reverse connection manager.
   * @param listener_ref Reference to the requesting listener.
   */
  virtual void registerRCInitiators(const Network::ListenerConfig& listener_ref) PURE;

  /**
   * Unregister a reverse connection creation request with the reverse connection manager.
   * @param listener_ref Reference to the requesting listener.
   */
  virtual void unregisterRCInitiator(const Network::ListenerConfig& listener_ref) PURE;

  /**
   * Notify all RCInitiatorss of connection closure, so that it can be removed from
   * internal maps a new connection created.
   * @param connectionKey the closed connection.
   * @param is_used true if a used connection gets closed, false if it is an idle one.
   */
  virtual void notifyConnectionClose(const std::string& connectionKey, bool is_used) PURE;

  /**
   * Mark connection as used, for stat logging purposes.
   * @param connectionKey the connection for which stats need to be updated.
   */
  virtual void markConnUsed(const std::string& connectionKey) PURE;

  /**
   * @return the number of active reverse connections from the local envoy
   * to the remote cluster key.
   */
  virtual uint64_t getNumberOfSockets(const std::string& key) PURE;

  /**
   * Obtain a mapping of remote cluster to number of initiated reverse connections.
   * @param return the cluster -> count of reverse conns mapping.
   */
  virtual absl::flat_hash_map<std::string, size_t> getSocketCountMap() PURE;
};
} // namespace Network
} // namespace Envoy
