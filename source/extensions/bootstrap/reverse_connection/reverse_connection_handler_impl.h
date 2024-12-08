#pragma once

#include <list>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

using RCHandlerStats = Network::RCHandlerStats;
using RCHandlerStatsPtr = Network::RCHandlerStatsPtr;
using RCSocketPair = Network::RCSocketPair;
using ReverseConnectionHandler = Network::ReverseConnectionHandler;

/**
 * implementation of Network::ReverseConnectionHandler.
 */
class ReverseConnectionHandlerImpl : public Network::ReverseConnectionHandler,
                                     Logger::Loggable<Logger::Id::filter> {
public:
  ReverseConnectionHandlerImpl(Event::Dispatcher* dispatcher);
  void addConnectionSocket(const std::string& node_id, const std::string& cluster_id,
                           Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
                           const std::chrono::seconds& ping_interval, bool rebalanced) override;
  void post(const std::string& node_id, const std::string& cluster_id,
            Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
            const std::chrono::seconds& ping_interval) override;
  RCSocketPair getConnectionSocket(const std::string& node_id, bool rebalanced) override;

  void rebalanceGetConnectionSocket(
      const std::string& key, bool rebalanced,
      std::shared_ptr<std::promise<Network::RCSocketPair>> socket_promise) override;
  size_t getNumberOfSocketsByNode(const std::string& node_id) override;
  size_t getNumberOfSocketsByCluster(const std::string& cluster) override;
  SocketCountMap getSocketCountMap() override;
  void markSocketDead(const int fd, const bool used) override;
  absl::flat_hash_map<std::string, size_t> getConnectionStats() override;
  // Constuct the RCHandler scope from the base scope and stat prefix and store it in
  // reverse_conn_handler_scope_.
  void initializeStats(Stats::Scope& scope) override;

  static const std::string ping_message;
  static absl::Mutex handler_lock;
  static std::vector<ReverseConnectionHandlerImpl*> handlers_;

private:
  void pingConnections();
  void pingConnections(const std::string& key);
  void tryEnablePingTimer(const std::chrono::seconds& ping_interval);
  void cleanStaleNodeEntry(const std::string& node_id);

  void onPingResponse(Network::IoHandle& io_handle);

  /** Pick the most appropriate handler for accepting a reverse connection. The
   * handler with the min number of accepted reverse connections will be picked.
   * @param node_id the node_id for which a handler needs to be picked.
   * @param cluster_id the cluster_id for which a handler needs to be picked.
   * @return the handler with the min number of accepted reverse connections for
   * node 'node_id'.
   */
  ReverseConnectionHandler& pickMinHandler(const std::string& node_id,
                                           const std::string& cluster_id);

  /** Pick the most appropriate handler for using a reverse connection.
   * @param node_id will be used to find the worker thread with a non-zero
   * number of accepted reverse connections for node 'node_id'. This is set
   * when a request is received with the 'x-dst-node-id' header set.
   * @param cluster_id will be used to find the worker thread with a non-zero
   * number of accepted reverse connections for cluster 'cluster_id'. This is set
   * when a request is received with the 'x-dst-cluster-id' header set.
   * @return the handler with a non-zero number of accepted reverse connections
   * for node 'node_id'.
   */
  ReverseConnectionHandler* pickTargetHandler(const std::string& node_id,
                                              const std::string& cluster_id);

  // Get or create a RCHandlerStats object for the given node.
  RCHandlerStats* getStatsByNode(const std::string& node_id);
  // Get or create a RCHandlerStats object for the given cluster.
  RCHandlerStats* getStatsByCluster(const std::string& cluster_id);
  // Delete the RCHandlerStats object for the given node.
  bool deleteStatsByNode(const std::string& node_id);
  // Delete the RCHandlerStats object for the given cluster.
  bool deleteStatsByCluster(const std::string& cluster_id);

  // Pointer to the thread local Dispatcher instance.
  Event::Dispatcher* dispatcher_;

  // Map of node IDs to connection sockets, stored on the accepting(remote) envoy.
  absl::flat_hash_map<std::string, std::list<Network::ConnectionSocketPtr>>
      accepted_reverse_connections_;
  absl::flat_hash_map<std::string, std::vector<std::string>> cluster_to_node_map_;

  // Map of node ID to the corresponding cluster it belongs to.
  absl::flat_hash_map<std::string, std::string> node_to_cluster_map_;

  // A map of the remote node ID -> RCHandlerStatsPtr, used to log accepted
  // reverse conn stats for every initiator node, by the local envoy as responder.
  absl::flat_hash_map<std::string, RCHandlerStatsPtr> rc_handler_node_stats_map_;

  // A map of the remote cluster ID -> RCHandlerStatsPtr, used to log accepted
  // reverse conn stats for every initiator cluster, by the local envoy as responder.
  absl::flat_hash_map<std::string, RCHandlerStatsPtr> rc_handler_cluster_stats_map_;

  // The scope for RCHandler stats.
  Stats::ScopeSharedPtr reverse_conn_handler_scope_;
  Event::TimerPtr ping_timer_;
  std::chrono::seconds ping_interval_{0};
  Random::RandomGeneratorPtr random_generator_;

  absl::flat_hash_map<int, std::string> fd_to_node_map_;
  absl::flat_hash_map<int, Event::FileEventPtr> fd_to_event_map_;
  absl::flat_hash_map<int, Event::TimerPtr> fd_to_timer_map_;
  // Set of FDs for all PCs that require proxy protocol to be
  // sent to them.
  // These are connections from an old PC to the transport hub.
  // TODO: This is tech-debt and eventually needs to be cleaned up.
  absl::flat_hash_set<int> expect_proxy_protocol_fd_set_;

  // Map of node IDs to the number of total accepted reverse connecitons
  // to the node. This is used to rebalance a request to accept reverse
  // connections to a different worker thread.
  absl::flat_hash_map<std::string, int> node_to_conn_count_map_;

  // Map of cluster IDs to the number of total accepted reverse connecitons
  // to the cluster. This is used to rebalance a request to accept reverse
  // connections to a different worker thread.
  absl::flat_hash_map<std::string, int> cluster_to_conn_count_map_;
};
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
