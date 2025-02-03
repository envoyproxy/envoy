#pragma once

#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Implementation of ReverseConnectionManager interface.
 */
class ReverseConnectionManagerImpl : public ReverseConnectionManager,
                                     public Logger::Loggable<Logger::Id::main> {
public:
  ReverseConnectionManagerImpl(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager);

  void initializeStats(Stats::Scope& scope) override;

  Event::Dispatcher& dispatcher() const override;

  Network::ConnectionHandler* connectionHandler() const override;

  void setConnectionHandler(Network::ConnectionHandler& conn_handler) override;

  Upstream::ClusterManager& clusterManager() const override;

  /**
   * Checks whether an existing ReverseConnectionInitiator is present for the given listener.
   * If not present, creates a new ReverseConnectionInitiator and returns it.
   */
  void findOrCreateRCInitiator(
      const Network::ListenerConfig& listener_ref, const std::string& src_node_id,
      const std::string& src_cluster_id, const std::string& src_tenant_id,
      const absl::flat_hash_map<std::string, uint32_t>& remote_cluster_to_conns) override;

  /**
   * Register a reverse connection creation request with the reverse connection manager.
   */
  void registerRCInitiators(Network::ConnectionHandler& conn_handler,
                            const Network::ListenerConfig& listener_ref) override;

  /**
   * Unregister a reverse connection creation request with the reverse connection manager.
   */
  void unregisterRCInitiator(const Network::ListenerConfig& listener_ref) override;

  /**
   * Add a connection -> RCInitiator mapping.
   */
  void registerConnection(const std::string& connectionKey,
                          ReverseConnectionInitiator* rc_inititator) override {
    connection_to_rc_initiator_map_[connectionKey] = rc_inititator;
  }

  /**
   * Unregister a connection and remove it from the connection -> RCInitiator mapping.
   */
  int unregisterConnection(const std::string& connectionKey) override {
    return connection_to_rc_initiator_map_.erase(connectionKey);
  }

  /**
   * Notify all RCInitiatorss of connection closure, so that it can be removed from
   * internal maps a new connection created.
   */
  void notifyConnectionClose(const std::string& connectionKey, bool is_used) override;

  /**
   * Mark connection as used, for stat logging purposes.
   */
  void markConnUsed(const std::string& connectionKey) override;

  /**
   * Return the number of active reverse connections from the local envoy
   * to the remote cluster key.
   */
  uint64_t getNumberOfSockets(const std::string& key) override;

  /**
   * Obtain a mapping of remote cluster to number of initiated reverse connections.
   */
  absl::flat_hash_map<std::string, size_t> getSocketCountMap() override;

  ReverseConnectionInitiator* getRCInitiatorPtr(const Network::ListenerConfig& listener_ref) override {
    const auto& available_rc_initiators_iter =
        available_rc_initiators_.find(listener_ref.listenerTag());
    if (available_rc_initiators_iter == available_rc_initiators_.end()) {
      return nullptr;
    }
    return available_rc_initiators_iter->second.get();
  }

private:

  // Callback to be called when a ReverseConnectionInitiator has been created.
  void createRCInitiatorDone(ReverseConnectionInitiator* initiator);

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
