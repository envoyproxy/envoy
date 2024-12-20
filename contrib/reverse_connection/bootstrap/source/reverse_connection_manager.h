#pragma once

#include <memory>

#include "envoy/network/connection_handler.h"


namespace Envoy {
namespace Upstream {
class ClusterManager;
}
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionInitiator;
using ReverseConnectionInitiator =
    Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionInitiator;
using RCInitiatorPtr = std::unique_ptr<ReverseConnectionInitiator>;

/**
 * The ReverseConnectionManager is used to manage the lifecycle of ReverseConnectionInitiators.
 * It creates a ReverseConnectionInitiator for each unique listener tag(name and version) with
 * reverse connection metadata, and uses it to create the requisite number of reverse conns
 * to the remote cluster. It reuses them if need be to re-create connections in cases of
 * socket closure.
 */
class ReverseConnectionManager : Logger::Loggable<Logger::Id::main> {
public:
  ReverseConnectionManager(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager);

  /**
   * Sets the stats scope for logging initiated reverse connections with the local
   * envoy as initiator.
   * @param scope the base scope to be used.
   * @return the parent scope for RCManager stats.
   */
  void initializeStats(Stats::Scope& scope);

  // Returns a reference to the parent dispatcher of the current thread.
  Event::Dispatcher& dispatcher() const;

  // Returns a reference to the connection handler.
  Network::ConnectionHandler* connectionHandler() const;

  // Returns a reference to the cluster manager.
  Upstream::ClusterManager& clusterManager() const;

  /**
   * Checks whether an existing ReverseConnectionInitiator is present for the given listener.
   * If not present, creates a new ReverseConnectionInitiator and returns it.
   * @param listener_ref Reference to the listener which is requesting the reverse connections.
   * @param src_node_id Source Node.
   * @param src_cluster_id Cluster from which reverse connections will be initiated, i.e., local
   * envoy.
   * @param src_tenant_id Tenant ID.
   * @param remote_cluster_to_conns A map with remote cluster IDs mapped to the number of reverse
   * conns requested for that remote cluster.
   * @return A unique pointer to the ReverseConnectionInitiator.
   */
  void findOrCreateRCInitiator(
      const Network::ListenerConfig& listener_ref, const std::string& src_node_id,
      const std::string& src_cluster_id, const std::string& src_tenant_id,
      const absl::flat_hash_map<std::string, uint32_t>& remote_cluster_to_conns);

  /**
   * Register a reverse connection creation request with the reverse connection manager.
   * @param conn_handler The connection handler.
   * @param listener_ref Reference to the requesting listener.
   */
  void registerRCInitiators(Network::ConnectionHandler& conn_handler,
                            const Network::ListenerConfig& listener_ref);

  /**
   * Unregister a reverse connection creation request with the reverse connection manager.
   * @param listener_ref Reference to the requesting listener.
   */
  void unregisterRCInitiator(const Network::ListenerConfig& listener_ref);

  /**
   * Add a connection -> RCInitiator mapping, this is used to pick an RCInitiator to
   * re-initiate a closed connection.
   * @param connectionKey the connection key of the closed connection.
   */
  void registerConnection(const std::string& connectionKey,
                          ReverseConnectionInitiator* rc_inititator) {
    connection_to_rc_initiator_map_[connectionKey] = rc_inititator;
  }

  /**
   * Unregister a connection and remove it from the connection -> RCInitiator mapping.
   * @param connectionKey the connection key of the closed connection.
   */
  int unregisterConnection(const std::string& connectionKey) {
    return connection_to_rc_initiator_map_.erase(connectionKey);
  }

  /**
   * Notify all RCInitiatorss of connection closure, so that it can be removed from
   * internal maps a new connection created.
   * @param connectionKey the closed connection.
   * @param is_used true if a used connection gets closed, false if it is an idle one.
   */
  void notifyConnectionClose(const std::string& connectionKey, bool is_used);

  /**
   * Mark connection as used, for stat logging purposes.
   * @param connectionKey the connection for which stats need to be updated.
   */
  void markConnUsed(const std::string& connectionKey);

  /**
   * @return the number of active reverse connections from the local envoy
   * to the remote cluster key.
   */
  uint64_t getNumberOfSockets(const std::string& key);

  /**
   * Obtain a mapping of remote cluster to number of initiated reverse connections.
   * @param return the cluster -> count of reverse conns mapping.
   */
  absl::flat_hash_map<std::string, size_t> getSocketCountMap();

  ReverseConnectionInitiator* getRCInitiatorPtr(const Network::ListenerConfig& listener_ref) {
    const auto& available_rc_initiators_iter =
        available_rc_initiators_.find(listener_ref.listenerTag());
    if (available_rc_initiators_iter == available_rc_initiators_.end()) {
      return nullptr;
    }
    return available_rc_initiators_iter->second.get();
  }

  void createRCInitiatorDone(ReverseConnectionInitiator* initiator);

private:
  // The parent dispatcher of the current thread.
  Event::Dispatcher& parent_dispatcher_;

  // The connection handler to pass the reverse connection socket to the listener that initiated it.
  Network::ConnectionHandler* conn_handler_;

  // The cluster manager to get the thread local cluster. This is required
  // to initiate reverse connections.
  Upstream::ClusterManager& cluster_manager_;

  /**
   * Map of connection key -> RCInitiator that created the connection.
   * This allows the RCManager to identify to identify which RCInitiator should
   * be called to re-initiate a closed connection.
   */
  absl::flat_hash_map<std::string, ReverseConnectionInitiator*> connection_to_rc_initiator_map_;
  /**
   * Map of listener name and version to the RCInitiator created.
   * This allows the ReverseConnectionManager to resuse a previously created
   * ReverseConnectionInitiator to initiate more reverse connections if a socket closes.
   */
  absl::flat_hash_map<uint64_t, RCInitiatorPtr> available_rc_initiators_;

  Stats::ScopeSharedPtr stats_root_scope_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
