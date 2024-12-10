#pragma once

#include <future>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/timer.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Network {

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
 * class to store reverse connection sockets.
 */
class ReverseConnectionHandler {
public:
  virtual ~ReverseConnectionHandler() = default;

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
  virtual void
  addConnectionSocket(const std::string& node_id, const std::string& cluster_id,
                      Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
                      const std::chrono::seconds& ping_interval = std::chrono::seconds::zero(),
                      bool rebalanced = false) PURE;

  /** Add the accepted connection and remote cluster mapping to RCHandler maps
   * through the thread local dispatcher.
   * @param node_id node_id of initiating node.
   * @param cluster_id cluster_id of receiving(acceptor) cluster.
   * @param socket the socket to be added.
   * @param expects_proxy_protocol whether the proxy protocol header is expected. This is used
   * in legacy versions.
   * @param ping_interval the interval at which ping keepalives are sent on accepted reverse conns.
   */
  virtual void post(const std::string& node_id, const std::string& cluster_id,
                    Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
                    const std::chrono::seconds& ping_interval = std::chrono::seconds::zero()) PURE;

  /** Called by the responder envoy when a request is received, that could be sent through a reverse
   * connection. This returns an accepted connection socket, if present.
   * @param key the remote cluster ID/ node ID.
   * @param rebalanced is true if we are calling the function after `pickTargetHandler` is used
   * to pick the most appropriate thread.
   */
  virtual std::pair<Network::ConnectionSocketPtr, bool> getConnectionSocket(const std::string& key,
                                                                            bool rebalanced) PURE;

  /** Called by the responder envoy when the local worker does not have any accepted reverse
   * connections for the key, to rebalance the request to a different worker and return the
   * connection socket.
   * @param key the remote cluster ID/ node ID.
   * @param rebalanced is true if we are calling the function after `pickTargetHandler` is used
   * to pick the most appropriate thread.
   * @param socket_promise the promise to be set with the connection socket.
   */
  virtual void rebalanceGetConnectionSocket(
      const std::string& key, bool rebalanced,
      std::shared_ptr<std::promise<Network::RCSocketPair>> socket_promise) PURE;

  /**
   * @return the number of reverse connections across all workers
   * for the given node id.
   */
  virtual size_t getNumberOfSocketsByNode(const std::string& node_id) PURE;

  /**
   * @return the number of reverse connections across all workers for
   * the given cluster id.
   */
  virtual size_t getNumberOfSocketsByCluster(const std::string& cluster_id) PURE;

  using SocketCountMap = absl::flat_hash_map<std::string, size_t>;
  /**
   *
   * @return the cluster -> reverse conn count mapping.
   */
  virtual SocketCountMap getSocketCountMap() PURE;
  /**
   * Mark the connection socket dead and remove it from internal maps.
   * @param fd the FD for the socket to be marked dead.
   * @param used is true, when the connection the fd belongs to has been used by a cluster.
   */
  virtual void markSocketDead(int fd, bool used) PURE;

  /**
   * Sets the stats scope for logging stats for accepted reverse connections
   * with the local envoy as responder.
   * @param scope the base scope to be used.
   * @return the parent scope for RCHandler stats.
   */
  virtual void initializeStats(Stats::Scope& scope) PURE;

  virtual absl::flat_hash_map<std::string, size_t> getConnectionStats() PURE;
};

} // namespace Network
} // namespace Envoy
