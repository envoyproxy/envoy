#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <list>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class ReverseTunnelAcceptorExtension;

/**
 * Thread-local socket manager for upstream reverse connections.
 */
class UpstreamSocketManager : public ThreadLocal::ThreadLocalObject,
                              public Logger::Loggable<Logger::Id::filter> {
  // Friend class for testing
  friend class TestUpstreamSocketManager;
  friend class TestUpstreamSocketManagerRebalancing;

public:
  UpstreamSocketManager(Event::Dispatcher& dispatcher,
                        ReverseTunnelAcceptorExtension* extension = nullptr);

  ~UpstreamSocketManager();

  /**
   * Add accepted connection to socket manager.
   * @param node_id node_id of initiating node.
   * @param cluster_id cluster_id of receiving cluster.
   * @param socket the socket to be added.
   * @param ping_interval the interval at which ping keepalives are sent.
   * @param rebalanced true if adding socket after rebalancing.
   */
  void addConnectionSocket(const std::string& node_id, const std::string& cluster_id,
                           Network::ConnectionSocketPtr socket,
                           const std::chrono::seconds& ping_interval, bool rebalanced = true);

  /**
   * Hand off a socket to this socket manager's dispatcher.
   * Used for cross-thread rebalancing of reverse connection sockets.
   * @param node_id node_id of initiating node.
   * @param cluster_id cluster_id of receiving cluster.
   * @param socket the socket to be added.
   * @param ping_interval the interval at which ping keepalives are sent.
   */
  void handoffSocketToWorker(const std::string& node_id, const std::string& cluster_id,
                             Network::ConnectionSocketPtr socket,
                             const std::chrono::seconds& ping_interval);

  /**
   * Get an available reverse connection socket.
   * @param node_id the node ID to get a socket for.
   * @return the connection socket, or nullptr if none available.
   */
  Network::ConnectionSocketPtr getConnectionSocket(const std::string& node_id);

  /**
   * Mark connection socket dead and remove from internal maps.
   * @param fd the FD for the socket to be marked dead.
   */
  void markSocketDead(const int fd);

  /**
   * Ping all active reverse connections for health checks.
   */
  void pingConnections();

  /**
   * Ping reverse connections for a specific node.
   * @param node_id the node ID whose connections should be pinged.
   */
  void pingConnections(const std::string& node_id);

  /**
   * Enable the ping timer if not already enabled.
   * @param ping_interval the interval at which ping keepalives should be sent.
   */
  void tryEnablePingTimer(const std::chrono::seconds& ping_interval);

  /**
   * Clean up stale node entries when no active sockets remain.
   * @param node_id the node ID to clean up.
   */
  void cleanStaleNodeEntry(const std::string& node_id);

  /**
   * Handle ping response from a reverse connection.
   * @param io_handle the IO handle for the socket that sent the ping response.
   */
  void onPingResponse(Network::IoHandle& io_handle);

  /**
   * Handle ping response timeout for a specific socket.
   * Increments miss count and marks socket dead if threshold reached.
   * @param fd the file descriptor whose ping timed out.
   */
  void onPingTimeout(int fd);

  /**
   * Set the miss threshold (consecutive misses before marking a socket dead).
   * @param threshold minimum value 1.
   */
  void setMissThreshold(uint32_t threshold) { miss_threshold_ = std::max<uint32_t>(1, threshold); }

  /**
   * Get the upstream extension for stats integration.
   * @return pointer to the upstream extension or nullptr if not available.
   */
  ReverseTunnelAcceptorExtension* getUpstreamExtension() const { return extension_; }

  /**
   * Get a node that has a socket (idle or used) for the given key.
   * If the key is found in the cluster_to_node_info_map_, assume it is the cluster ID and return a
   * node in that cluster in a round-robin manner. If the key is not found in the
   * cluster_to_node_info_map_, assume it is the node ID and return it as-is.
   * @param key the cluster ID or node ID to lookup.
   * @return the node ID, or the key itself if it cannot be resolved.
   */
  std::string getNodeWithSocket(const std::string& key);

  /**
   * Pick the least loaded socket manager across all worker threads for a given node.
   * @param node_id the node ID to find the least loaded manager for.
   * @param cluster_id the cluster ID for logging purposes.
   * @return reference to the least loaded socket manager.
   */
  UpstreamSocketManager& pickLeastLoadedSocketManager(const std::string& node_id,
                                                      const std::string& cluster_id);

private:
  /**
   * Helper method to check if a node has any reverse connection sockets (idle or used).
   * @param node_id the node ID to check.
   * @return true if the node has any sockets, false otherwise.
   */
  bool hasAnySocketsForNode(const std::string& node_id);

  // Thread local dispatcher instance.
  Event::Dispatcher& dispatcher_;
  Random::RandomGeneratorPtr random_generator_;

  // Map of node IDs to connection sockets.
  absl::flat_hash_map<std::string, std::list<Network::ConnectionSocketPtr>>
      accepted_reverse_connections_;

  // Map from file descriptor to node ID. An entry is added when a reverse tunnel is accepted from a
  // node and is removed when the socket dies.
  absl::flat_hash_map<int, std::string> fd_to_node_map_;

  // Map from file descriptor to cluster ID. An entry is added when a reverse tunnel is accepted
  // from a node and is removed when the socket dies.
  absl::flat_hash_map<int, std::string> fd_to_cluster_map_;

  // Map of node ID to cluster, for all nodes that have a reverse tunnel socket.
  absl::flat_hash_map<std::string, std::string> node_to_cluster_map_;

  // Cluster information for tracking member nodes.
  struct ClusterInfo {
    // List of node IDs that belong to this cluster and have any sockets (idle or used).
    std::vector<std::string> nodes;
    // Round-robin index for load distribution when selecting member nodes.
    size_t round_robin_index = 0;
  };

  // Map of cluster IDs to cluster node information.
  // A cluster entry is added when a reverse tunnel is accepted from a node in that cluster
  // and is removed only when all nodes in the cluster have no remaining sockets.
  absl::flat_hash_map<std::string, ClusterInfo> cluster_to_node_info_map_;

  // File events and timers for ping functionality.
  absl::flat_hash_map<int, Event::FileEventPtr> fd_to_event_map_;
  absl::flat_hash_map<int, Event::TimerPtr> fd_to_timer_map_;

  // Track consecutive ping misses per file descriptor.
  absl::flat_hash_map<int, uint32_t> fd_to_miss_count_;
  // Miss threshold before declaring a socket dead.
  static constexpr uint32_t kDefaultMissThreshold = 3;
  uint32_t miss_threshold_{kDefaultMissThreshold};

  Event::TimerPtr ping_timer_;
  std::chrono::seconds ping_interval_{0};

  // Upstream extension for stats integration.
  ReverseTunnelAcceptorExtension* extension_;

  // Map of node IDs to the number of total accepted reverse connections
  // for the node. This is used to rebalance a request to accept reverse
  // connections to a different worker thread.
  absl::flat_hash_map<std::string, int> node_to_conn_count_map_;

  // Global list of all socket managers across threads for rebalancing.
  static std::vector<UpstreamSocketManager*> socket_managers_;
  static absl::Mutex socket_manager_lock;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
