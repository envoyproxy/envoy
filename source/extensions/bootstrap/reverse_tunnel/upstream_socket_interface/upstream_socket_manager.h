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
                           const std::chrono::seconds& ping_interval, bool rebalanced);

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
   * Automatically discern whether the key is a node ID or cluster ID.
   * @param key the key to get the node ID for.
   * @return the node ID.
   */
  std::string getNodeID(const std::string& key);

private:
  // Thread local dispatcher instance.
  Event::Dispatcher& dispatcher_;
  Random::RandomGeneratorPtr random_generator_;

  // Map of node IDs to connection sockets.
  absl::flat_hash_map<std::string, std::list<Network::ConnectionSocketPtr>>
      accepted_reverse_connections_;

  // Map from file descriptor to node ID.
  absl::flat_hash_map<int, std::string> fd_to_node_map_;

  // Map of node ID to cluster.
  absl::flat_hash_map<std::string, std::string> node_to_cluster_map_;

  // Map of cluster IDs to node IDs.
  absl::flat_hash_map<std::string, std::vector<std::string>> cluster_to_node_map_;

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
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
