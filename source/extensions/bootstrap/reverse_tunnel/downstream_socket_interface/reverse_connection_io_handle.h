#pragma once

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/network/filter_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/rc_connection_wrapper.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_load_balancer_context.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations.
class ReverseTunnelInitiatorExtension;
class ReverseConnectionIOHandle;

namespace {
// HTTP protocol constants.
static constexpr absl::string_view kCrlf = "\r\n";
static constexpr absl::string_view kDoubleCrlf = "\r\n\r\n";

// Connection timing constants.
static constexpr uint32_t kDefaultMaxReconnectAttempts = 10;
} // namespace

/**
 * Connection state tracking for reverse connections.
 */
enum class ReverseConnectionState {
  Connecting,    // Connection is being established (handshake initiated).
  Connected,     // Connection has been successfully established.
  Recovered,     // Connection has recovered from a previous failure.
  Failed,        // Connection establishment failed during handshake.
  CannotConnect, // Connection cannot be initiated (early failure).
  Backoff        // Connection is in backoff state due to failures.
};

/**
 * Configuration for remote cluster connections.
 * Defines connection parameters for each remote cluster that reverse connections should be
 * established to.
 */
struct RemoteClusterConnectionConfig {
  std::string cluster_name;          // Name of the remote cluster.
  uint32_t reverse_connection_count; // Number of reverse connections to maintain per host.
  // TODO(basundhara-c): Implement retry logic using max_reconnect_attempts for connections to this
  // cluster. This is the max reconnection attempts made for a cluster when the initial reverse
  // connection attempt fails.
  uint32_t max_reconnect_attempts; // Maximum number of reconnection attempts.

  RemoteClusterConnectionConfig(const std::string& name, uint32_t count,
                                uint32_t max_attempts = kDefaultMaxReconnectAttempts)
      : cluster_name(name), reverse_connection_count(count), max_reconnect_attempts(max_attempts) {}
};

/**
 * Configuration for reverse connection socket interface.
 */
struct ReverseConnectionSocketConfig {
  std::string src_cluster_id; // Cluster identifier of local envoy instance.
  std::string src_node_id;    // Node identifier of local envoy instance.
  std::string src_tenant_id;  // Tenant identifier of local envoy instance.
  // TODO(basundhara-c): Add support for multiple remote clusters using the same
  // ReverseConnectionIOHandle. Currently, each ReverseConnectionIOHandle handles
  // reverse connections for a single upstream cluster since a different ReverseConnectionAddress
  // is created for different upstream clusters. Eventually, we should embed metadata for
  // multiple remote clusters in the same ReverseConnectionAddress and therefore should be able
  // to use a single ReverseConnectionIOHandle for multiple remote clusters.
  std::vector<RemoteClusterConnectionConfig>
      remote_clusters;         // List of remote cluster configurations.
  bool enable_circuit_breaker; // Whether to place a cluster in backoff when reverse connection
                               // attempts fail.
  ReverseConnectionSocketConfig() : enable_circuit_breaker(true) {}
};

/**
 * This class handles the lifecycle of reverse connections, including establishment,
 * maintenance, and cleanup of connections to remote clusters.
 * At this point, a ReverseConnectionIOHandle is created for each upstream cluster.
 * This is because a different ReverseConnectionAddress is created for each upstream cluster.
 * This ReverseConnectionIOHandle initiates TCP connections to each host of the upstream cluster,
 * and caches the IOHandle for serving requests coming from the upstream cluster.
 */
class ReverseConnectionIOHandle : public Network::IoSocketHandleImpl,
                                  public Network::ConnectionCallbacks {
  // Define friend classes for testing.
  friend class ReverseConnectionIOHandleTest;
  friend class RCConnectionWrapperTest;
  friend class DownstreamReverseConnectionIOHandleTest;

public:
  /**
   * Constructor for ReverseConnectionIOHandle.
   * @param fd the file descriptor for listener socket.
   * @param config the configuration for reverse connections.
   * @param cluster_manager the cluster manager for accessing upstream clusters.
   * @param extension the extension for stats updates.
   * @param scope the stats scope for metrics collection.
   */
  ReverseConnectionIOHandle(os_fd_t fd, const ReverseConnectionSocketConfig& config,
                            Upstream::ClusterManager& cluster_manager,
                            ReverseTunnelInitiatorExtension* extension, Stats::Scope& scope);

  ~ReverseConnectionIOHandle() override;

  // Network::IoHandle overrides.
  /**
   * Override of listen method for reverse connections.
   * No-op for reverse connections.
   * @param backlog the listen backlog.
   * @return SysCallIntResult with success status.
   */
  Api::SysCallIntResult listen(int backlog) override;

  /**
   * Override of accept method for reverse connections.
   * Returns established reverse connections when they become available. This is woken up using the
   * trigger pipe when a tcp connection to an upstream cluster is established.
   * @param addr pointer to store the client address information.
   * @param addrlen pointer to the length of the address structure.
   * @return IoHandlePtr for the accepted reverse connection, or nullptr if none available.
   */
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;

  /**
   * Override of read method for reverse connections.
   * @param buffer the buffer to read data into.
   * @param max_length optional maximum number of bytes to read.
   * @return IoCallUint64Result indicating the result of the read operation.
   */
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;

  /**
   * Override of write method for reverse connections.
   * @param buffer the buffer containing data to write.
   * @return IoCallUint64Result indicating the result of the write operation.
   */
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;

  /**
   * Override of connect method for reverse connections.
   * For reverse connections, this is not used since we connect to the upstream clusters in
   * initializeFileEvent().
   * @param address the target address (unused for reverse connections).
   * @return SysCallIntResult with success status.
   */
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;

  /**
   * Override of close method for reverse connections.
   * @return IoCallUint64Result indicating the result of the close operation.
   */
  Api::IoCallUint64Result close() override;

  /**
   * Triggers the reverse connection workflow.
   * @param dispatcher the event dispatcher.
   * @param cb the file ready callback.
   * @param trigger the file trigger type.
   * @param events the events to monitor.
   */
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;

  // Network::ConnectionCallbacks.
  /**
   * Called when connection events occur.
   * For reverse connections, we handle these events through RCConnectionWrapper.
   * @param event the connection event that occurred.
   */
  void onEvent(Network::ConnectionEvent event) override;

  /**
   * No-op for reverse connections.
   */
  void onAboveWriteBufferHighWatermark() override {}

  /**
   * No-op for reverse connections.
   */
  void onBelowWriteBufferLowWatermark() override {}

  /**
   * Get the file descriptor for the pipe monitor used to wake up accept().
   * @return the file descriptor for the pipe monitor
   */
  int getPipeMonitorFd() const;

  // Callbacks from RCConnectionWrapper.
  /**
   * Called when a reverse connection handshake completes. This method wakes up accept() if the
   * reverse connection handshake was successful. If not, it performs necessary cleanup and triggers
   * backoff for the host.
   * @param error error message if the handshake failed, empty string if successful.
   * @param wrapper pointer to the connection wrapper that wraps over the established connection.
   * @param closed whether the connection was closed during handshake.
   */
  void onConnectionDone(const std::string& error, RCConnectionWrapper* wrapper, bool closed);

  // Backoff logic for connection failures.
  /**
   * Determine if connections should be initiated to a host, i.e., if host is in backoff period.
   * @param host_address the address of the host to check.
   * @param cluster_name the name of the cluster the host belongs to.
   * @return true if connection attempt should be made, false if in backoff.
   */
  bool shouldAttemptConnectionToHost(const std::string& host_address,
                                     const std::string& cluster_name);

  /**
   * Track a connection failure for a specific host and cluster and trigger backoff logic.
   * @param host_address the address of the host that failed.
   * @param cluster_name the name of the cluster the host belongs to.
   */
  void trackConnectionFailure(const std::string& host_address, const std::string& cluster_name);

  /**
   * Reset backoff state for a specific host. Called when a connection is established successfully.
   * @param host_address the address of the host to reset backoff for.
   */
  void resetHostBackoff(const std::string& host_address);

  /**
   * Update the connection state for a specific connection and update metrics.
   * @param host_address the address of the host.
   * @param cluster_name the name of the cluster.
   * @param connection_key the unique key identifying the connection.
   * @param new_state the new state to set for the connection.
   */
  void updateConnectionState(const std::string& host_address, const std::string& cluster_name,
                             const std::string& connection_key, ReverseConnectionState new_state);

  /**
   * Update state-specific gauge using switch case logic (combined increment/decrement).
   * @param host_address the address of the host
   * @param cluster_name the name of the cluster
   * @param state the connection state to update
   * @param increment whether to increment (true) or decrement (false) the gauge
   */
  void updateStateGauge(const std::string& host_address, const std::string& cluster_name,
                        ReverseConnectionState state, bool increment);

  /**
   * Remove connection state tracking for a specific connection.
   * @param host_address the address of the host.
   * @param cluster_name the name of the cluster.
   * @param connection_key the unique key identifying the connection.
   */
  void removeConnectionState(const std::string& host_address, const std::string& cluster_name,
                             const std::string& connection_key);

  /**
   * Handle downstream connection closure and update internal maps so that the next
   * maintenance cycle re-initiates the connection.
   * @param connection_key the unique key identifying the closed connection.
   */
  void onDownstreamConnectionClosed(const std::string& connection_key);

  /**
   * Get reference to the cluster manager.
   * @return reference to the cluster manager
   */
  Upstream::ClusterManager& getClusterManager() { return cluster_manager_; }

  /**
   * Get pointer to the downstream extension for stats updates.
   * @return pointer to the extension, nullptr if not available
   */
  ReverseTunnelInitiatorExtension* getDownstreamExtension() const;

private:
  /**
   * Get time source for consistent time operations.
   * @return reference to the time source
   */
  TimeSource& getTimeSource() const;

  /**
   * @return reference to the thread-local dispatcher
   */
  Event::Dispatcher& getThreadLocalDispatcher() const;

  /**
   * Check if thread-local dispatcher is available.
   * @return true if dispatcher is available and safe to use
   */
  bool isThreadLocalDispatcherAvailable() const;

  /**
   * Create the trigger mechanism used to wake up accept() when connections are established.
   */
  void createTriggerMechanism();

  // Functions to maintain connections to remote clusters.
  /**
   * Maintain reverse connections for all configured clusters.
   * Initiates and maintains the required number of connections to each remote cluster.
   */
  void maintainReverseConnections();

  /**
   * Maintain reverse connections for a specific cluster.
   * @param cluster_name the name of the cluster to maintain connections for
   * @param cluster_config the configuration for the cluster
   */
  void maintainClusterConnections(const std::string& cluster_name,
                                  const RemoteClusterConnectionConfig& cluster_config);

  /**
   * Initiate a single reverse connection to a specific host.
   * @param cluster_name the name of the cluster the host belongs to
   * @param host_address the address of the host to connect to
   * @param host the host object containing connection information
   * @return true if connection initiation was successful, false otherwise
   */
  bool initiateOneReverseConnection(const std::string& cluster_name,
                                    const std::string& host_address,
                                    Upstream::HostConstSharedPtr host);

  /**
   * Clean up all reverse connection resources.
   * Called during shutdown to properly close connections and free resources.
   */
  void cleanup();

  // Pipe trigger mechanism helpers
  /**
   * Create trigger pipe used to wake up accept() when a connection is established.
   */
  void createTriggerPipe();

  /**
   * Check if trigger pipe is ready for use.
   * @return true if initialized and ready
   */
  bool isTriggerPipeReady() const;

  // Host/cluster mapping management
  /**
   * Update cluster -> host mappings from the cluster manager. Called before connection initiation
   * to a cluster.
   * @param cluster_id the ID of the cluster
   * @param hosts the list of hosts in the cluster
   */
  void maybeUpdateHostsMappingsAndConnections(const std::string& cluster_id,
                                              const std::vector<std::string>& hosts);

  /**
   * Remove stale host entries and close associated connections.
   * @param host the address of the host to remove
   */
  void removeStaleHostAndCloseConnections(const std::string& host);

  /**
   * Per-host connection tracking for better management.
   * Contains all information needed to track and manage connections to a specific host.
   */
  struct HostConnectionInfo {
    std::string host_address;                                // Host address
    std::string cluster_name;                                // Cluster to which host belongs
    absl::flat_hash_set<std::string> connection_keys;        // Connection keys for stats tracking
    uint32_t target_connection_count;                        // Target connection count for the host
    uint32_t failure_count{0};                               // Number of consecutive failures
    std::chrono::steady_clock::time_point last_failure_time; // NO_CHECK_FORMAT(real_time)
    std::chrono::steady_clock::time_point backoff_until;     // NO_CHECK_FORMAT(real_time)
    absl::flat_hash_map<std::string, ReverseConnectionState>
        connection_states; // State tracking per connection
  };

  // Map from host address to connection info.
  absl::flat_hash_map<std::string, HostConnectionInfo> host_to_conn_info_map_;
  // Map from cluster name to set of resolved hosts
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> cluster_to_resolved_hosts_map_;

  // Core components
  const ReverseConnectionSocketConfig config_; // Configuration for reverse connections
  Upstream::ClusterManager& cluster_manager_;
  ReverseTunnelInitiatorExtension* extension_;

  // Connection wrapper management
  std::vector<std::unique_ptr<RCConnectionWrapper>>
      connection_wrappers_; // Active connection wrappers
  // Mapping from wrapper to host. This designates the number of successful connections to a host.
  absl::flat_hash_map<RCConnectionWrapper*, std::string> conn_wrapper_to_host_map_;

  // Simple pipe-based trigger mechanism to wake up accept() when a connection is established.
  // Inlined directly for simplicity and reduced test coverage requirements.
  int trigger_pipe_read_fd_{-1};
  int trigger_pipe_write_fd_{-1};

  // Connection management : We store the established connections in a queue.
  // and pop the last established connection when data is read on trigger_pipe_read_fd_
  // to determine the connection that got established last.
  std::queue<Envoy::Network::ClientConnectionPtr> established_connections_;

  // Single retry timer for all clusters
  Event::TimerPtr rev_conn_retry_timer_;

  bool is_reverse_conn_started_{
      false}; // Whether reverse connections have been started on worker thread
  Event::Dispatcher* worker_dispatcher_{nullptr}; // Dispatcher for the worker thread

  // Store original socket FD for cleanup.
  os_fd_t original_socket_fd_{-1};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
