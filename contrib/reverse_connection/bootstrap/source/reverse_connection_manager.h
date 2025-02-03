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
class ReverseConnectionManager {
public:
  virtual ~ReverseConnectionManager() = default;

  /**
   * Sets the stats scope for logging initiated reverse connections with the local
   * envoy as initiator.
   * @param scope the base scope to be used.
   * @return the parent scope for RCManager stats.
   */
  virtual void initializeStats(Stats::Scope& scope) PURE;

  // Returns a reference to the parent dispatcher of the current thread.
  virtual Event::Dispatcher& dispatcher() const PURE;

  // Returns a reference to the connection handler.
  virtual Network::ConnectionHandler* connectionHandler() const PURE;

  // Sets the connection handler.
  virtual void setConnectionHandler(Network::ConnectionHandler& conn_handler) PURE;

  // Returns a reference to the cluster manager.
  virtual Upstream::ClusterManager& clusterManager() const PURE;

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
  virtual void findOrCreateRCInitiator(
      const Network::ListenerConfig& listener_ref, const std::string& src_node_id,
      const std::string& src_cluster_id, const std::string& src_tenant_id,
      const absl::flat_hash_map<std::string, uint32_t>& remote_cluster_to_conns) PURE;

  /**
   * Register a reverse connection creation request with the reverse connection manager.
   * @param conn_handler The connection handler.
   * @param listener_ref Reference to the requesting listener.
   */
  virtual void registerRCInitiators(Network::ConnectionHandler& conn_handler,
                            const Network::ListenerConfig& listener_ref) PURE;

  /**
   * Unregister a reverse connection creation request with the reverse connection manager.
   * @param listener_ref Reference to the requesting listener.
   */
  virtual void unregisterRCInitiator(const Network::ListenerConfig& listener_ref) PURE;

  /**
   * Add a connection -> RCInitiator mapping, this is used to pick an RCInitiator to
   * re-initiate a closed connection.
   * @param connectionKey the connection key of the closed connection.
   */
  virtual void registerConnection(const std::string& connectionKey,
                          ReverseConnectionInitiator* rc_inititator) PURE;

  /**
   * Unregister a connection and remove it from the connection -> RCInitiator mapping.
   * @param connectionKey the connection key of the closed connection.
   */
  virtual int unregisterConnection(const std::string& connectionKey) PURE;

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

  /**
   * Returns the ReverseConnectionInitiator owning reverse connections for a given
   * listener tag.
   * @param listener_ref the listener for which the RCInitiator is requested.
   * @return the RCInitiator for the listener.
   */
  virtual ReverseConnectionInitiator* getRCInitiatorPtr(const Network::ListenerConfig& listener_ref) PURE;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
