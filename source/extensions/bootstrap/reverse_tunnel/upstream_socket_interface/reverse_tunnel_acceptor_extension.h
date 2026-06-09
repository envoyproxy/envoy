#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/reverse_tunnel_reporter.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/config/utility.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_lifecycle_info.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class UpstreamSocketManager;
class ReverseTunnelAcceptorExtension;
class ReverseTunnelAcceptor;

/**
 * Thread local storage for ReverseTunnelAcceptor.
 */
class UpstreamSocketThreadLocal : public ThreadLocal::ThreadLocalObject {
public:
  /**
   * Creates a new socket manager instance for the given dispatcher.
   * @param dispatcher the thread-local dispatcher.
   * @param extension the upstream extension for stats integration.
   */
  UpstreamSocketThreadLocal(Event::Dispatcher& dispatcher,
                            ReverseTunnelAcceptorExtension* extension = nullptr);

  /**
   * @return reference to the thread-local dispatcher.
   */
  Event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * @return pointer to the thread-local socket manager.
   */
  UpstreamSocketManager* socketManager() { return socket_manager_.get(); }
  const UpstreamSocketManager* socketManager() const { return socket_manager_.get(); }

  // Per-worker tracking of unique clusters and nodes (no mutex needed - single worker thread).
  // Maps track connection counts per cluster/node. Size of map = number of unique clusters/nodes.
  absl::flat_hash_map<std::string, uint64_t> cluster_connection_counts_;
  absl::flat_hash_map<std::string, uint64_t> node_connection_counts_;
  // Per-worker aggregate metrics gauges.
  Stats::Gauge* total_clusters_gauge_{nullptr};
  Stats::Gauge* total_nodes_gauge_{nullptr};

private:
  // Thread-local dispatcher.
  Event::Dispatcher& dispatcher_;
  // Thread-local socket manager.
  std::unique_ptr<UpstreamSocketManager> socket_manager_;
};

/**
 * Socket interface extension for upstream reverse connections.
 */
class ReverseTunnelAcceptorExtension
    : public Envoy::Network::SocketInterfaceExtension,
      public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
  // Friend class for testing.
  friend class ReverseTunnelAcceptorExtensionTest;

public:
  /**
   * @param sock_interface the reverse tunnel acceptor to extend.
   * @param context the server factory context.
   * @param config the configuration for this extension.
   */
  ReverseTunnelAcceptorExtension(
      ReverseTunnelAcceptor& sock_interface, Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
          UpstreamReverseConnectionSocketInterface& config);

  ~ReverseTunnelAcceptorExtension() override;

  /**
   * Called when the server is initialized.
   */
  void onServerInitialized(Server::Instance&) override;

  /**
   * Called when a worker thread is initialized.
   */
  void onWorkerThreadInitialized() override {}

  /**
   * @return pointer to the thread-local registry, or nullptr if not available.
   */
  UpstreamSocketThreadLocal* getLocalRegistry() const;

  /**
   * @return reference to the stat prefix string.
   */
  const std::string& statPrefix() const { return stat_prefix_; }

  /**
   * @return the configured miss threshold for ping health-checks.
   */
  uint32_t pingFailureThreshold() const { return ping_failure_threshold_; }

  /**
   * Synchronous version for admin API endpoints that require immediate response on reverse
   * connection stats.
   * @param timeout_ms maximum time to wait for aggregation completion
   * @return pair of <connected_nodes, accepted_connections> or empty if timeout
   */
  std::pair<std::vector<std::string>, std::vector<std::string>>
  getConnectionStatsSync(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds(5000));

  /**
   * Get cross-worker aggregated reverse connection stats.
   * @return map of node/cluster -> connection count across all worker threads.
   */
  absl::flat_hash_map<std::string, uint64_t> getCrossWorkerStatMap();

  /**
   * Update the cross-thread aggregated stats for the connection.
   * @param node_id the node identifier for the connection.
   * @param cluster_id the cluster identifier for the connection.
   * @param increment whether to increment (true) or decrement (false) the connection count.
   */
  void updateConnectionStats(const std::string& node_id, const std::string& cluster_id,
                             bool increment, bool tenant_isolation_enabled = false);

  /**
   * Get per-worker connection stats for debugging.
   * @return map of node/cluster -> connection count for the current worker thread.
   */
  absl::flat_hash_map<std::string, uint64_t> getPerWorkerStatMap();

  /**
   * Get the stats scope for accessing global stats.
   * @return reference to the stats scope.
   */
  Stats::Scope& getStatsScope() const { return context_.scope(); }

  /**
   * @return whether tenant isolation is enabled.
   */
  bool enableTenantIsolation() const { return enable_tenant_isolation_; }

  /**
   * @return whether lifecycle access logs are configured.
   */
  bool hasAccessLogs() const { return !access_logs_.empty(); }

  /**
   * Emit a lifecycle access log using a synthetic StreamInfo built from reverse-tunnel metadata.
   */
  void emitSyntheticLifecycleLog(
      absl::string_view event, const ReverseTunnelLifecycleInfo& lifecycle, TimeSource& time_source,
      AccessLog::AccessLogType access_log_type = AccessLog::AccessLogType::NotSet,
      absl::string_view handoff_kind = {}, absl::string_view close_reason = {},
      const LifecycleLogMetadata& extra_metadata = LifecycleLogMetadata{}) const;

  /**
   * Emit a lifecycle access log using an existing connection-backed StreamInfo.
   */
  void emitConnectionLifecycleLog(
      absl::string_view event, StreamInfo::StreamInfo& stream_info,
      const ReverseTunnelLifecycleInfo& lifecycle,
      AccessLog::AccessLogType access_log_type = AccessLog::AccessLogType::NotSet,
      absl::string_view handoff_kind = {}, absl::string_view close_reason = {},
      const LifecycleLogMetadata& extra_metadata = LifecycleLogMetadata{}) const;

  /**
   * Forward a connection event to the configured reporter.
   * If no reporter is present, the call is ignored.
   * @param node_id node to which the connection is made.
   * @param cluster_id cluster which the node belongs to.
   * @param tenant_id tenant identifier supplied by the peer.
   */
  void reportConnection(absl::string_view node_id, absl::string_view cluster_id,
                        absl::string_view tenant_id) {
    if (reporter_ != nullptr) {
      reporter_->reportConnectionEvent(node_id, cluster_id, tenant_id);
    }
  }

  /**
   * Forward a disconnection event to the configured reporter.
   * If no reporter is present, the call is ignored.
   * @param node_id node to which the connection is made.
   * @param cluster_id cluster which the node belongs to.
   */
  void reportDisconnection(absl::string_view node_id, absl::string_view cluster_id) {
    if (reporter_ != nullptr) {
      reporter_->reportDisconnectionEvent(node_id, cluster_id);
    }
  }

  /**
   * Test-only method to set the thread local slot.
   * @param slot the thread local slot to set.
   */
  void
  setTestOnlyTLSRegistry(std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> slot) {
    tls_slot_ = std::move(slot);
  }

  /**
   * Test-only method to replace lifecycle access loggers.
   */
  void setTestOnlyAccessLogs(AccessLog::InstanceSharedPtrVector access_logs) {
    access_logs_ = std::move(access_logs);
  }

private:
  void emitLifecycleLog(const Formatter::Context& log_context,
                        const StreamInfo::StreamInfo& stream_info) const;
  void populateLifecycleStreamInfo(StreamInfo::StreamInfo& stream_info,
                                   const ReverseTunnelLifecycleInfo& lifecycle,
                                   absl::string_view event, absl::string_view handoff_kind,
                                   absl::string_view close_reason,
                                   const LifecycleLogMetadata& extra_metadata) const;
  Server::Configuration::ServerFactoryContext& context_;
  // Thread-local slot for storing the socket manager per worker thread.
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  ReverseTunnelAcceptor* socket_interface_;
  std::string stat_prefix_;
  uint32_t ping_failure_threshold_{3};
  bool enable_detailed_stats_{false};
  bool enable_tenant_isolation_{false};
  AccessLog::InstanceSharedPtrVector access_logs_;
  ReverseTunnelReporterPtr reporter_{nullptr};

  /**
   * Update per-worker aggregate metrics (total_clusters and total_nodes).
   * This is an internal function called only from updateConnectionStats.
   * @param node_id the node identifier for the connection.
   * @param cluster_id the cluster identifier for the connection.
   * @param increment whether to increment (true) or decrement (false) the connection count.
   */
  void updatePerWorkerAggregateMetrics(const std::string& node_id, const std::string& cluster_id,
                                       bool increment);

  /**
   * Update per-worker connection stats for debugging.
   * This is an internal function called only from updateConnectionStats.
   * @param node_id the node identifier for the connection.
   * @param cluster_id the cluster identifier for the connection.
   * @param increment whether to increment (true) or decrement (false) the connection count.
   */
  void updatePerWorkerConnectionStats(const std::string& node_id, const std::string& cluster_id,
                                      bool increment, bool tenant_isolation_enabled);
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
