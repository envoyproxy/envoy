#pragma once

#include <memory>

#include "envoy/network/connection_handler.h"

#include "source/extensions/bootstrap/reverse_connection/reverse_connection_initiator.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionInitiator;
using ReverseConnectionInitiator =
    Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionInitiator;
using RCInitiatorPtr = std::unique_ptr<ReverseConnectionInitiator>;

/**
 * The ReverseConnectionManagerImpl is used to manage the lifecycle of ReverseConnectionInitiators.
 * It creates a ReverseConnectionInitiator for each unique listener tag(name and version) with
 * reverse connection metadata, and uses it to create the requisite number of reverse conns
 * to the remote cluster. It reuses them if need be to re-create connections in cases of
 * socket closure.
 */
class ReverseConnectionManagerImpl : Logger::Loggable<Logger::Id::main>,
                                     public Envoy::Network::ReverseConnectionManager {
public:
  ReverseConnectionManagerImpl(Event::Dispatcher& dispatcher);

  // Constuct the RCManager scope from the base scope and stat prefix and store it in
  // reverse_conn_manager_scope_.
  void initializeStats(Stats::Scope& scope) override;

  // Returns a reference to the parent dispatcher of the current thread.
  Event::Dispatcher& dispatcher() const override;

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
   * Register a reverse connection creation request with the reverse connection manager. This will
   * be called by the listener manager.
   * @param listener_ref Reference to the listener which is requesting the reverse connections.
   */
  void registerRCInitiators(const Network::ListenerConfig& listener_ref) override;

  /**
   * Unregister a reverse connection creation request with the reverse connection manager. This is
   * called by the listener manager when a listener is drained. Checks if a rc initiator is present,
   * for the listener. If so, removes it from the map.
   * @param listener_ref Reference to the listener which is requesting the reverse connections.
   */
  void unregisterRCInitiator(const Network::ListenerConfig& listener_ref) override;

  // Registers the connection -> RCInitiator mapping, used to re-initiate reverse conns.
  void registerConnection(const std::string& connectionKey,
                          ReverseConnectionInitiator* rc_inititator) {
    connection_to_rc_initiator_map_[connectionKey] = rc_inititator;
  }
  // Removes the connection -> RCInitiator mapping.
  int unregisterConnection(const std::string& connectionKey) {
    return connection_to_rc_initiator_map_.erase(connectionKey);
  }
  // Notifies RCInitiators of connection closure.
  void notifyConnectionClose(const std::string& connectionKey, bool is_used) override;

  // Mark the connection as used.
  void markConnUsed(const std::string& connectionKey) override;

  uint64_t getNumberOfSockets(const std::string& key) override;

  absl::flat_hash_map<std::string, size_t> getSocketCountMap() override;

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

  /**
   * Map of connection key -> RCInitiator that created the connection.
   * This allows the RCManager to identify to identify which RCInitiator should
   * be called to re-initiate a closed connection.
   */
  absl::flat_hash_map<std::string, ReverseConnectionInitiator*> connection_to_rc_initiator_map_;
  /**
   * Map of listener name and version to the RCInitiator created.
   * This allows the ReverseConnectionManagerImpl to resuse a previously created
   * ReverseConnectionInitiator to initiate more reverse connections if a socket closes.
   */
  absl::flat_hash_map<uint64_t, RCInitiatorPtr> available_rc_initiators_;

  Stats::ScopeSharedPtr stats_root_scope_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
