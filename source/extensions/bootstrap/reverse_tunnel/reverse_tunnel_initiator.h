#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "envoy/api/io_error.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/reverse_connection_socket_interface.pb.validate.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/network/filter_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/upstream/load_balancer_context_base.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class RCConnectionWrapper;
class ReverseTunnelInitiator;
class ReverseTunnelInitiatorExtension;

static const char CRLF[] = "\r\n";
static const char DOUBLE_CRLF[] = "\r\n\r\n";

/**
 * All reverse connection downstream stats. @see stats_macros.h
 */
#define ALL_REVERSE_CONNECTION_DOWNSTREAM_STATS(GAUGE)                                             \
  GAUGE(reverse_conn_connecting, NeverImport)                                                      \
  GAUGE(reverse_conn_connected, NeverImport)                                                       \
  GAUGE(reverse_conn_failed, NeverImport)                                                          \
  GAUGE(reverse_conn_recovered, NeverImport)                                                       \
  GAUGE(reverse_conn_backoff, NeverImport)                                                         \
  GAUGE(reverse_conn_cannot_connect, NeverImport)

/**
 * Connection state tracking for reverse connections.
 */
enum class ReverseConnectionState {
  Connecting,    // Connection is being established (handshake initiated)
  Connected,     // Connection has been successfully established
  Recovered,     // Connection has recovered from a previous failure
  Failed,        // Connection establishment failed during handshake
  CannotConnect, // Connection cannot be initiated (early failure)
  Backoff        // Connection is in backoff state due to failures
};

/**
 * Struct definition for all reverse connection downstream stats. @see stats_macros.h
 */
struct ReverseConnectionDownstreamStats {
  ALL_REVERSE_CONNECTION_DOWNSTREAM_STATS(GENERATE_GAUGE_STRUCT)
};

using ReverseConnectionDownstreamStatsPtr = std::unique_ptr<ReverseConnectionDownstreamStats>;

/**
 * Configuration for remote cluster connections.
 * Defines connection parameters for each remote cluster that reverse connections should be
 * established to.
 */
struct RemoteClusterConnectionConfig {
  std::string cluster_name;          // Name of the remote cluster
  uint32_t reverse_connection_count; // Number of reverse connections to maintain per host
  uint32_t reconnect_interval_ms;    // Interval between reconnection attempts in milliseconds
  uint32_t max_reconnect_attempts;   // Maximum number of reconnection attempts
  bool enable_health_check;          // Whether to enable health checks for this cluster

  RemoteClusterConnectionConfig(const std::string& name, uint32_t count,
                                uint32_t reconnect_ms = 5000, uint32_t max_attempts = 10,
                                bool health_check = true)
      : cluster_name(name), reverse_connection_count(count), reconnect_interval_ms(reconnect_ms),
        max_reconnect_attempts(max_attempts), enable_health_check(health_check) {}
};

/**
 * Configuration for reverse connection socket interface.
 */
struct ReverseConnectionSocketConfig {
  std::string src_cluster_id; // Cluster identifier of local envoy instance
  std::string src_node_id;    // Node identifier of local envoy instance
  std::string src_tenant_id;  // Tenant identifier of local envoy instance
  std::vector<RemoteClusterConnectionConfig>
      remote_clusters;               // List of remote cluster configurations
  uint32_t health_check_interval_ms; // Interval for health checks in milliseconds
  uint32_t connection_timeout_ms;    // Connection timeout in milliseconds
  bool enable_metrics;               // Whether to enable metrics collection
  bool enable_circuit_breaker;       // Whether to enable circuit breaker functionality

  ReverseConnectionSocketConfig()
      : health_check_interval_ms(30000), connection_timeout_ms(10000), enable_metrics(true),
        enable_circuit_breaker(true) {}
};

/**
 * This class handles the lifecycle of reverse connections, including establishment,
 * maintenance, and cleanup of connections to remote clusters.
 */
class ReverseConnectionIOHandle : public Network::IoSocketHandleImpl,
                                  public Network::ConnectionCallbacks {
public:
  /**
   * Constructor for ReverseConnectionIOHandle.
   * @param fd the file descriptor for listener socket
   * @param config the configuration for reverse connections
   * @param cluster_manager the cluster manager for accessing upstream clusters
   * @param socket_interface reference to the parent socket interface
   * @param scope the stats scope for metrics collection
   */
  ReverseConnectionIOHandle(os_fd_t fd, const ReverseConnectionSocketConfig& config,
                            Upstream::ClusterManager& cluster_manager,
                            const ReverseTunnelInitiator& socket_interface, Stats::Scope& scope);

  ~ReverseConnectionIOHandle() override;

  // Network::IoHandle overrides
  /**
   * Override of listen method for reverse connections.
   * Initiates reverse connection establishment to configured remote clusters.
   * @param backlog the listen backlog (unused for reverse connections)
   * @return SysCallIntResult with success status
   */
  Api::SysCallIntResult listen(int backlog) override;

  /**
   * Override of accept method for reverse connections.
   * Returns established reverse connections when they become available. This is woken up using the
   * trigger pipe when a tcp connection to an upstream cluster is established.
   * @param addr pointer to store the client address information
   * @param addrlen pointer to the length of the address structure
   * @return IoHandlePtr for the accepted reverse connection, or nullptr if none available
   */
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;

  /**
   * Override of read method for reverse connections.
   * @param buffer the buffer to read data into
   * @param max_length optional maximum number of bytes to read
   * @return IoCallUint64Result indicating the result of the read operation
   */
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;

  /**
   * Override of write method for reverse connections.
   * @param buffer the buffer containing data to write
   * @return IoCallUint64Result indicating the result of the write operation
   */
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;

  /**
   * Override of connect method for reverse connections.
   * For reverse connections, this is not used since we connect to the upstream clusters in
   * listen().
   * @param address the target address (unused for reverse connections)
   * @return SysCallIntResult with success status
   */
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;

  /**
   * Override of close method for reverse connections.
   * @return IoCallUint64Result indicating the result of the close operation
   */
  Api::IoCallUint64Result close() override;

  // Network::ConnectionCallbacks
  /**
   * Called when connection events occur.
   * For reverse connections, we handle these events through RCConnectionWrapper.
   * @param event the connection event that occurred
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
   * Check if trigger pipe is ready for accepting connections.
   * @return true if the trigger pipe is both the FDs are ready
   */
  bool isTriggerPipeReady() const;

  // Callbacks from RCConnectionWrapper
  /**
   * Called when a reverse connection handshake completes.
   * @param error error message if the handshake failed, empty string if successful
   * @param wrapper pointer to the connection wrapper that wraps over the established connection
   * @param closed whether the connection was closed during handshake
   */
  void onConnectionDone(const std::string& error, RCConnectionWrapper* wrapper, bool closed);

  // Backoff logic for connection failures
  /**
   * Determine if connections should be initiated to a host, i.e., if host is in backoff period.
   * @param host_address the address of the host to check
   * @param cluster_name the name of the cluster the host belongs to
   * @return true if connection attempt should be made, false if in backoff
   */
  bool shouldAttemptConnectionToHost(const std::string& host_address,
                                     const std::string& cluster_name);

  /**
   * Track a connection failure for a specific host and cluster and apply backoff logic.
   * @param host_address the address of the host that failed
   * @param cluster_name the name of the cluster the host belongs to
   */
  void trackConnectionFailure(const std::string& host_address, const std::string& cluster_name);

  /**
   * Reset backoff state for a specific host. Called when a connection is established successfully.
   * @param host_address the address of the host to reset backoff for
   */
  void resetHostBackoff(const std::string& host_address);

  /**
   * Initialize stats collection for reverse connections.
   * @param scope the stats scope to use for metrics collection
   */
  void initializeStats(Stats::Scope& scope);

  /**
   * Get or create stats for a specific cluster.
   * @param cluster_name the name of the cluster to get stats for
   * @return pointer to the cluster stats
   */
  ReverseConnectionDownstreamStats* getStatsByCluster(const std::string& cluster_name);

  /**
   * Get or create stats for a specific host within a cluster.
   * @param host_address the address of the host to get stats for
   * @param cluster_name the name of the cluster the host belongs to
   * @return pointer to the host stats
   */
  ReverseConnectionDownstreamStats* getStatsByHost(const std::string& host_address,
                                                   const std::string& cluster_name);

  /**
   * Update the connection state for a specific connection and update metrics.
   * @param host_address the address of the host
   * @param cluster_name the name of the cluster
   * @param connection_key the unique key identifying the connection
   * @param new_state the new state to set for the connection
   */
  void updateConnectionState(const std::string& host_address, const std::string& cluster_name,
                             const std::string& connection_key, ReverseConnectionState new_state);

  /**
   * Remove connection state tracking for a specific connection.
   * @param host_address the address of the host
   * @param cluster_name the name of the cluster
   * @param connection_key the unique key identifying the connection
   */
  void removeConnectionState(const std::string& host_address, const std::string& cluster_name,
                             const std::string& connection_key);

  /**
   * Handle downstream connection closure and trigger re-initiation.
   * @param connection_key the unique key identifying the closed connection
   */
  void onDownstreamConnectionClosed(const std::string& connection_key);

  /**
   * Increment the gauge for a specific connection state.
   * @param cluster_stats pointer to cluster-level stats
   * @param host_stats pointer to host-level stats
   * @param state the connection state to increment
   */
  void incrementStateGauge(ReverseConnectionDownstreamStats* cluster_stats,
                           ReverseConnectionDownstreamStats* host_stats,
                           ReverseConnectionState state);

  /**
   * Decrement the gauge for a specific connection state.
   * @param cluster_stats pointer to cluster-level stats
   * @param host_stats pointer to host-level stats
   * @param state the connection state to decrement
   */
  void decrementStateGauge(ReverseConnectionDownstreamStats* cluster_stats,
                           ReverseConnectionDownstreamStats* host_stats,
                           ReverseConnectionState state);

private:
  /**
   * @return reference to the thread-local dispatcher
   */
  Event::Dispatcher& getThreadLocalDispatcher() const;

  /**
   * Create the trigger pipe used to wake up accept() when connections are established.
   */
  void createTriggerPipe();

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
    std::string host_address;                         // Host address
    std::string cluster_name;                         // Cluster to which host belongs
    absl::flat_hash_set<std::string> connection_keys; // Connection keys for stats tracking
    uint32_t target_connection_count;                 // Target connection count for the host
    uint32_t failure_count{0};                        // Number of consecutive failures
    std::chrono::steady_clock::time_point last_failure_time{
        std::chrono::steady_clock::now()}; // Time of last failure
    std::chrono::steady_clock::time_point backoff_until{
        std::chrono::steady_clock::now()}; // Backoff end time
    absl::flat_hash_map<std::string, ReverseConnectionState>
        connection_states; // State tracking per connection
  };

  // Map from host address to connection info.
  std::unordered_map<std::string, HostConnectionInfo> host_to_conn_info_map_;
  // Map from cluster name to set of resolved hosts
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> cluster_to_resolved_hosts_map_;

  // Core components
  const ReverseConnectionSocketConfig config_; // Configuration for reverse connections
  Upstream::ClusterManager& cluster_manager_;
  const ReverseTunnelInitiator& socket_interface_;

  // Connection wrapper management
  std::vector<std::unique_ptr<RCConnectionWrapper>>
      connection_wrappers_; // Active connection wrappers
  // Mapping from wrapper to host. This designates the number of successful connections to a host.
  std::unordered_map<RCConnectionWrapper*, std::string> conn_wrapper_to_host_map_;

  // Pipe used to wake up accept() when a connection is established.
  // We write a single byte to the write end of the pipe when the reverse
  // connection request is accepted and read the byte in the accept() call.
  // This, along with the established_connections_ queue, is used to
  // determine the connection that got established last.
  int trigger_pipe_read_fd_{-1};
  int trigger_pipe_write_fd_{-1};

  // Connection management : We store the established connections in a queue
  // and pop the last established connection when data is read on trigger_pipe_read_fd_
  // to determine the connection that got established last.
  std::queue<Envoy::Network::ClientConnectionPtr> established_connections_;

  // Socket cache to prevent socket objects from going out of scope
  // Maps connection key to socket object.
  // Socket cache removed - sockets are now managed via RAII in DownstreamReverseConnectionIOHandle

  // Stats tracking per cluster and host
  absl::flat_hash_map<std::string, ReverseConnectionDownstreamStatsPtr> cluster_stats_map_;
  absl::flat_hash_map<std::string, ReverseConnectionDownstreamStatsPtr> host_stats_map_;
  Stats::ScopeSharedPtr reverse_conn_scope_; // Stats scope for reverse connections

  // Single retry timer for all clusters
  Event::TimerPtr rev_conn_retry_timer_;

  bool listening_initiated_{false}; // Whether reverse connections have been initiated
};

/**
 * Thread local storage for ReverseTunnelInitiator.
 * Stores the thread-local dispatcher and stats scope for each worker thread.
 */
class DownstreamSocketThreadLocal : public ThreadLocal::ThreadLocalObject {
public:
  DownstreamSocketThreadLocal(Event::Dispatcher& dispatcher, Stats::Scope& scope)
      : dispatcher_(dispatcher), scope_(scope) {}

  /**
   * @return reference to the thread-local dispatcher
   */
  Event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * @return reference to the stats scope
   */
  Stats::Scope& scope() { return scope_; }

private:
  Event::Dispatcher& dispatcher_;
  Stats::Scope& scope_;
};

/**
 * Socket interface that creates reverse connection sockets.
 * This class implements the SocketInterface interface to provide reverse connection
 * functionality for downstream connections. It manages the establishment and maintenance
 * of reverse TCP connections to remote clusters.
 */
class ReverseTunnelInitiator : public Envoy::Network::SocketInterfaceBase,
                               public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
public:
  ReverseTunnelInitiator(Server::Configuration::ServerFactoryContext& context);

  // Default constructor for registry
  ReverseTunnelInitiator() : extension_(nullptr), context_(nullptr) {}

  /**
   * Create a ReverseConnectionIOHandle and kick off the reverse connection establishment.
   * @param socket_type the type of socket to create
   * @param addr_type the address type
   * @param version the IP version
   * @param socket_v6only whether to create IPv6-only socket
   * @param options socket creation options
   * @return IoHandlePtr for the created socket, or nullptr for unsupported types
   */
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type, Envoy::Network::Address::Type addr_type,
         Envoy::Network::Address::IpVersion version, bool socket_v6only,
         const Envoy::Network::SocketCreationOptions& options) const override;

  // No-op for reverse connections.
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type,
         const Envoy::Network::Address::InstanceConstSharedPtr addr,
         const Envoy::Network::SocketCreationOptions& options) const override;

  /**
   * @return true if the IP family is supported
   */
  bool ipFamilySupported(int domain) override;

  /**
   * @return pointer to the thread-local registry, or nullptr if not available
   */
  DownstreamSocketThreadLocal* getLocalRegistry() const;

  /**
   * Thread-safe helper method to create reverse connection socket with config.
   * @param socket_type the type of socket to create
   * @param addr_type the address type
   * @param version the IP version
   * @param config the reverse connection configuration
   * @return IoHandlePtr for the reverse connection socket
   */
  Envoy::Network::IoHandlePtr
  createReverseConnectionSocket(Envoy::Network::Socket::Type socket_type,
                                Envoy::Network::Address::Type addr_type,
                                Envoy::Network::Address::IpVersion version,
                                const ReverseConnectionSocketConfig& config) const;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.bootstrap.reverse_connection.downstream_reverse_connection_socket_interface";
  }

  /**
   * Get the number of established reverse connections to a specific target (cluster or node).
   * @param target the cluster or node name to check connections for
   * @return number of established connections to the target
   */
  size_t getConnectionCount(const std::string& target) const;

  /**
   * Get a list of all clusters that have established reverse connections.
   * @return vector of cluster names with active reverse connections
   */
  std::vector<std::string> getEstablishedConnections() const;

private:
  ReverseTunnelInitiatorExtension* extension_;
  Server::Configuration::ServerFactoryContext* context_;
};

/**
 * Bootstrap extension for ReverseTunnelInitiator.
 */
class ReverseTunnelInitiatorExtension : public Server::BootstrapExtension,
                                        public Logger::Loggable<Logger::Id::connection> {
public:
  ReverseTunnelInitiatorExtension(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
          DownstreamReverseConnectionSocketInterface& config);

  void onServerInitialized() override;
  void onWorkerThreadInitialized() override {}

  /**
   * @return pointer to the thread-local registry, or nullptr if not available
   */
  DownstreamSocketThreadLocal* getLocalRegistry() const;

private:
  Server::Configuration::ServerFactoryContext& context_;
  const envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;
  ThreadLocal::TypedSlotPtr<DownstreamSocketThreadLocal> tls_slot_;
};

DECLARE_FACTORY(ReverseTunnelInitiator);

/**
 * Custom load balancer context for reverse connections. This class enables the
 * ReverseConnectionIOHandle to propagate upstream host details to the cluster_manager, ensuring
 * that connections are initiated to specified hosts rather than random ones. It inherits
 * from the LoadBalancerContextBase class and overrides the `overrideHostToSelect` method.
 */
class ReverseConnectionLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  ReverseConnectionLoadBalancerContext(const std::string& host_to_select) {
    host_to_select_ = std::make_pair(host_to_select, false);
  }

  /**
   * @return optional OverrideHost specifying the host to initiate reverse connection to.
   */
  absl::optional<OverrideHost> overrideHostToSelect() const override {
    return absl::make_optional(host_to_select_);
  }

private:
  OverrideHost host_to_select_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
