#pragma once

#include <list>
#include <string>
#include <future>
#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {


/**
 * All ReverseConnectionHandler stats. @see stats_macros.h
 * This encompasses the stats for all accepted reverse connections by the responder envoy.
 * The initiated reverse connections by the initiator envoy are logged by the RCManager.
 */
#define ALL_RCHANDLER_STATS(GAUGE)                                                                 \
  GAUGE(reverse_conn_cx_idle, NeverImport)                                                         \
  GAUGE(reverse_conn_cx_used, NeverImport)                                                         \
  GAUGE(reverse_conn_cx_total, NeverImport)

/**
 * Struct definition for all ReverseConnectionHandler stats. @see stats_macros.h
 */
struct RCHandlerStats {
  ALL_RCHANDLER_STATS(GENERATE_GAUGE_STRUCT)
};

using RCHandlerStatsPtr = std::unique_ptr<RCHandlerStats>;
using RCSocketPair = std::pair<Network::ConnectionSocketPtr, bool>;

/**
 * ReverseConnectionHandler is used to store accepted reverse connection sockets.
 */
class ReverseConnectionHandler : Logger::Loggable<Logger::Id::filter> {
public:
  ReverseConnectionHandler(Event::Dispatcher* dispatcher);

  /** Add the accepted connection and remote cluster mapping to RCHandler maps.
   * @param node_id node_id of initiating node.
   * @param cluster_id cluster_id of receiving(acceptor) cluster.
   * @param socket the socket to be added.
   * @param expects_proxy_protocol whether the proxy protocol header is expected. This is used
   * in legacy versions.
   * @param ping_interval the interval at which ping keepalives are sent on accepted reverse conns.
   * @param rebalanced is true if we are adding to the socket after `pickMinHandler` is used
   * to pick the most appropriate thread.
   */
  void addConnectionSocket(const std::string& node_id, const std::string& cluster_id,
                           Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
                           const std::chrono::seconds& ping_interval, bool rebalanced);
  
  /** Add the accepted connection and remote cluster mapping to RCHandler maps
   * through the thread local dispatcher.
   * @param node_id node_id of initiating node.
   * @param cluster_id cluster_id of receiving(acceptor) cluster.
   * @param socket the socket to be added.
   * @param expects_proxy_protocol whether the proxy protocol header is expected. This is used
   * in legacy versions.
   * @param ping_interval the interval at which ping keepalives are sent on accepted reverse conns.
   */
  void post(const std::string& node_id, const std::string& cluster_id,
            Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
            const std::chrono::seconds& ping_interval);

  /** Called by the responder envoy when a request is received, that could be sent through a reverse
   * connection. This returns an accepted connection socket, if present.
   * @param key the remote cluster ID/ node ID.
   * @param rebalanced is true if we are calling the function after `pickTargetHandler` is used
   * to pick the most appropriate thread.
   */
  RCSocketPair getConnectionSocket(const std::string& node_id, bool rebalanced);

  /** Called by the responder envoy when the local worker does not have any accepted reverse
   * connections for the key, to rebalance the request to a different worker and return the
   * connection socket.
   * @param key the remote cluster ID/ node ID.
   * @param rebalanced is true if we are calling the function after `pickTargetHandler` is used
   * to pick the most appropriate thread.
   * @param socket_promise the promise to be set with the connection socket.
   */
  void rebalanceGetConnectionSocket(
      const std::string& key, bool rebalanced,
      std::shared_ptr<std::promise<RCSocketPair>> socket_promise);

  /**
   * @return the number of reverse connections across all workers
   * for the given node id.
   */
  size_t getNumberOfSocketsByNode(const std::string& node_id);

  /**
   * @return the number of reverse connections across all workers for
   * the given cluster id.
   */
  size_t getNumberOfSocketsByCluster(const std::string& cluster);

  using SocketCountMap = absl::flat_hash_map<std::string, size_t>;

  /**
   *
   * @return the cluster -> reverse conn count mapping.
   */
  SocketCountMap getSocketCountMap();

  /**
   * Mark the connection socket dead and remove it from internal maps.
   * @param fd the FD for the socket to be marked dead.
   * @param used is true, when the connection the fd belongs to has been used by a cluster.
   */
  void markSocketDead(const int fd, const bool used);

  /**
   * Return the node -> reverse conn count mapping.
   */
  absl::flat_hash_map<std::string, size_t> getConnectionStats();

  /**
   * Sets the stats scope for logging stats for accepted reverse connections
   * with the local envoy as responder.
   * @param scope the base scope to be used.
   * @return the parent scope for RCHandler stats.
   */
  void initializeStats(Stats::Scope& scope);

  static const std::string ping_message;
  static absl::Mutex handler_lock;
  static std::vector<ReverseConnectionHandler*> handlers_;

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
