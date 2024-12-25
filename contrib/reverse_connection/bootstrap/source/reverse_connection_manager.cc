#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_initiator.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseConnectionManager::ReverseConnectionManager(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager)
    : parent_dispatcher_(dispatcher), cluster_manager_(cluster_manager) {
  ASSERT(parent_dispatcher_.isThreadSafe());
}

void ReverseConnectionManager::initializeStats(Stats::Scope& scope) {
  const std::string stats_prefix = "reverse_conn_manager.";
  stats_root_scope_ = scope.createScope(stats_prefix);
  ENVOY_LOG(debug, "Initialized RCManager stats; scope: {}",
            stats_root_scope_->constSymbolTable().toString(stats_root_scope_->prefix()));
}

Event::Dispatcher& ReverseConnectionManager::dispatcher() const {
  return parent_dispatcher_;
}

Network::ConnectionHandler* ReverseConnectionManager::connectionHandler() const {
  ASSERT(conn_handler_ != nullptr, "Connection Handler is not set");
  return conn_handler_;
}

Upstream::ClusterManager& ReverseConnectionManager::clusterManager() const {
  return cluster_manager_;
}

void ReverseConnectionManager::findOrCreateRCInitiator(
    const Network::ListenerConfig& listener_ref, const std::string& src_node_id,
    const std::string& src_cluster_id, const std::string& src_tenant_id,
    const absl::flat_hash_map<std::string, uint32_t>& remote_cluster_to_conns) {

  ENVOY_LOG(
      debug,
      "RCManager: Checking whether RC initiator is present for listener: name:{} tag:{} version:{}",
      listener_ref.name(), listener_ref.listenerTag(), listener_ref.versionInfo());
  const uint64_t rc_initiator_key = listener_ref.listenerTag();

  if (available_rc_initiators_.find(rc_initiator_key) == available_rc_initiators_.end()) {
    ENVOY_LOG(debug,
              "RCManager: No existing RC initiator for listener tag: {}, Creating new RC initiator",
              rc_initiator_key);
    ENVOY_LOG(debug, "src_node_id: {}", src_node_id);       // remove
    ENVOY_LOG(debug, "src_cluster_id: {}", src_cluster_id); // remove
    ENVOY_LOG(debug, "src_tenant_id: {}", src_tenant_id);   // remove
    for (const auto& iter : remote_cluster_to_conns) {
      ENVOY_LOG(trace, "remote_cluster_id: {} conn_count: {}", iter.first, iter.second); // remove
    }
    ReverseConnectionInitiator::ReverseConnectionOptions rc_options = {
        src_node_id,            // Source Node ID
        src_cluster_id,         // Source Cluster ID
        src_tenant_id,          // Source Tenant ID
        remote_cluster_to_conns // Remote cluster -> number of connections map
    };
    ENVOY_LOG(debug, "RCManager: Creating new RC initiator for listener tag: {}", rc_initiator_key);
    ASSERT(stats_root_scope_ != nullptr);
    ENVOY_LOG(trace, "Posting to dispatcher from parent_dispatcher_: {} dispatcher().name: {}",
              parent_dispatcher_.name(), dispatcher().name()); // remove
    dispatcher().post(
        [this, rc_initiator_key, &listener_ref, rc_options = std::move(rc_options)]() {
          ENVOY_LOG(debug,
                    "Creating ReverseConnectionInitiator on dispatcher {} parent_dispatcher_: {} "
                    "for listener tag: {}",
                    dispatcher().name(), parent_dispatcher_.name(), rc_initiator_key);
          available_rc_initiators_[rc_initiator_key] = std::make_unique<ReverseConnectionInitiator>(
              listener_ref, std::move(rc_options), *this, *stats_root_scope_);
          createRCInitiatorDone(available_rc_initiators_[rc_initiator_key].get());
        });
  } else {
    ENVOY_LOG(debug, "RCManager: Using existing RC initiator");
    createRCInitiatorDone(available_rc_initiators_[rc_initiator_key].get());
  }
  return;
}

void ReverseConnectionManager::createRCInitiatorDone(ReverseConnectionInitiator* initiator) {
  ENVOY_LOG(debug, "RCManager: createRCInitiatorDone");
  const bool success = initiator->maintainConnCount();
  ENVOY_LOG(debug, "RCManager: reverse connection initiation finished with status: {}", success);
}

void ReverseConnectionManager::registerRCInitiators(
    Network::ConnectionHandler& conn_handler,
    const Network::ListenerConfig& listener_ref) {
  // The conn handler needs to be set once for the thread local RCManager.
  if (conn_handler_ == nullptr) {
    ENVOY_LOG(debug, "RCManager: Setting connection handler for the first time");
    conn_handler_ = &conn_handler;
  }
  const std::string& src_node_id =
      listener_ref.reverseConnectionListenerConfig()->getReverseConnParams()->src_node_id_;
  const std::string& src_cluster_id =
      listener_ref.reverseConnectionListenerConfig()->getReverseConnParams()->src_cluster_id_;
  const std::string& src_tenant_id =
      listener_ref.reverseConnectionListenerConfig()->getReverseConnParams()->src_tenant_id_;
  absl::flat_hash_map<std::string, uint32_t>& remote_cluster_to_conns =
      listener_ref.reverseConnectionListenerConfig()
          ->getReverseConnParams()
          ->remote_cluster_to_conn_count_map_;

  ENVOY_LOG(debug,
            "RCManager: Received reverse connection initiation request for listener name: {} "
            "tag:{} version:{} on worker: {}",
            listener_ref.name(), listener_ref.listenerTag(), listener_ref.versionInfo(),
            dispatcher().name());
  findOrCreateRCInitiator(listener_ref, src_node_id, src_cluster_id, src_tenant_id,
                          remote_cluster_to_conns);
  // const bool success = rc_initiator_ptr->maintainConnCount();
  // ENVOY_LOG(debug, "RCManager: reverse connection initiation finished with status: {}", success);
}

void ReverseConnectionManager::unregisterRCInitiator(
    const Network::ListenerConfig& listener_ref) {

  ENVOY_LOG(
      debug,
      "RCManager: Destroying reverse connections initiator for listener: {} tag:{} version:{}",
      listener_ref.name(), listener_ref.listenerTag(), listener_ref.versionInfo());
  const uint64_t rc_initiator_key = listener_ref.listenerTag();
  auto iter = available_rc_initiators_.find(rc_initiator_key);
  if (iter != available_rc_initiators_.end()) {
    ENVOY_LOG(debug, "RCManager: Found reverse connections initiator");
    iter->second.reset();
    available_rc_initiators_.erase(iter);
  }
}

void ReverseConnectionManager::notifyConnectionClose(const std::string& connectionKey,
                                                         bool is_used) {
  ENVOY_LOG(debug, "RCManager: Connection closure reported for connection key: {}", connectionKey);
  ENVOY_LOG(debug, "RCManager: Searching for connection key {} in connection_to_rc_initiator_map_",
            connectionKey);
  const auto& iter = connection_to_rc_initiator_map_.find(connectionKey);
  if (iter == connection_to_rc_initiator_map_.end()) {
    ENVOY_LOG(debug,
              "RCManager: Could not find connection key {} in connection_to_rc_initiator_map_",
              connectionKey);
    return;
  }
  ReverseConnectionInitiator* rc_initiator = iter->second;
  ENVOY_LOG(debug,
            "RCManager: Found rc_initiator {} for connection key {} in "
            "connection_to_rc_initiator_map_. Notifying connection closure",
            rc_initiator->getID(), connectionKey);
  rc_initiator->notifyConnectionClose(connectionKey, is_used);
  unregisterConnection(connectionKey);
  return;
}

void ReverseConnectionManager::markConnUsed(const std::string& connectionKey) {
  ENVOY_LOG(debug, "RCManager: Marking connection with key: {} as used", connectionKey);
  const auto& iter = connection_to_rc_initiator_map_.find(connectionKey);
  if (iter == connection_to_rc_initiator_map_.end()) {
    ENVOY_LOG(debug,
              "RCManager: Could not find connection key {} in connection_to_rc_initiator_map_",
              connectionKey);
    return;
  }
  ReverseConnectionInitiator* rc_initiator = iter->second;
  rc_initiator->markConnUsed(connectionKey);
}

uint64_t ReverseConnectionManager::getNumberOfSockets(const std::string& key) {
  uint64_t number = 0;
  for (const auto& iter : available_rc_initiators_) {
    number += iter.second->getNumberOfSockets(key);
  }
  return number;
}

absl::flat_hash_map<std::string, size_t> ReverseConnectionManager::getSocketCountMap() {
  absl::flat_hash_map<std::string, size_t> response;
  for (const auto& iter : available_rc_initiators_) {
    iter.second->getSocketCountMap(response);
  }
  return response;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
