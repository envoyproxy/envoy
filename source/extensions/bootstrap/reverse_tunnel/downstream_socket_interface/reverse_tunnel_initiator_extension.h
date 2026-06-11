#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/server/generic_factory_context.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class DownstreamSocketThreadLocal;

/**
 * Bootstrap extension for ReverseTunnelInitiator.
 */
class ReverseTunnelInitiatorExtension : public Server::BootstrapExtension,
                                        public Logger::Loggable<Logger::Id::connection> {
  // Friend class for testing
  friend class ReverseTunnelInitiatorExtensionTest;

public:
  ReverseTunnelInitiatorExtension(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
          DownstreamReverseConnectionSocketInterface& config);

  void onServerInitialized(Server::Instance&) override;
  void onWorkerThreadInitialized() override;

  /**
   * @return reference to the stat prefix string.
   */
  const std::string& statPrefix() const { return stat_prefix_; }

  /**
   * @return pointer to the thread-local registry, or nullptr if not available.
   */
  DownstreamSocketThreadLocal* getLocalRegistry() const;

  /**
   * Update all connection stats for reverse connections. This updates the cross-worker stats
   * as well as the per-worker stats.
   * @param node_id the node identifier for the connection
   * @param cluster_id the cluster identifier for the connection
   * @param state_suffix the state suffix (e.g., "connecting", "connected", "failed")
   * @param increment whether to increment (true) or decrement (false) the connection count
   */
  void updateConnectionStats(const std::string& node_id, const std::string& cluster_id,
                             const std::string& state_suffix, bool increment);

  /**
   * Get per-worker stat map for the current dispatcher.
   * @return map of stat names to values for the current worker thread
   */
  absl::flat_hash_map<std::string, uint64_t> getPerWorkerStatMap();

  /**
   * Get cross-worker stat map across all workers.
   * @return map of stat names to values across all worker threads
   */
  absl::flat_hash_map<std::string, uint64_t> getCrossWorkerStatMap();

  /**
   * Get connection stats synchronously with timeout.
   * @param timeout_ms timeout for the operation
   * @return pair of vectors containing connected nodes and accepted connections
   */
  std::pair<std::vector<std::string>, std::vector<std::string>>
  getConnectionStatsSync(std::chrono::milliseconds timeout_ms);

  /**
   * Get the stats scope for accessing stats.
   * @return reference to the stats scope.
   */
  Stats::Scope& getStatsScope() const { return context_.scope(); }

  /**
   * @return reference to the configured HTTP handshake request path.
   */
  const std::string& handshakeRequestPath() const { return handshake_request_path_; }

  /**
   * @return reference to the additional headers to include in the handshake request.
   */
  const std::vector<envoy::config::core::v3::HeaderValueOption>&
  handshakeAdditionalHeaders() const {
    return additional_headers_;
  }

  /**
   * @return whether the handshake is negotiated as an HTTP/1.1 Upgrade exchange.
   */
  bool handshakeUsesHttpUpgrade() const { return use_http_upgrade_; }

  /**
   * @return reference to the configured access loggers for reverse tunnel lifecycle events.
   */
  const AccessLog::InstanceSharedPtrVector& accessLogs() const { return access_logs_; }

  /**
   * Emit an access log entry for a reverse tunnel lifecycle event.
   * Creates an ephemeral StreamInfo populated with dynamic metadata containing
   * reverse tunnel identifiers and event details.
   * @param time_source the time source for the stream info
   * @param event the lifecycle event name
   * @param node_id the node identifier of this Envoy instance
   * @param cluster_id the cluster identifier of this Envoy instance
   * @param tenant_id the tenant identifier of this Envoy instance
   * @param upstream_cluster the name of the upstream cluster
   * @param host_address the address of the remote host
   * @param connection_key the unique key identifying the connection
   * @param error_message the error message (empty on success)
   */
  void emitAccessLog(TimeSource& time_source, const std::string& event, const std::string& node_id,
                     const std::string& cluster_id, const std::string& tenant_id,
                     const std::string& upstream_cluster, const std::string& host_address,
                     const std::string& connection_key, const std::string& error_message);

  /**
   * Increment handshake stats for reverse tunnel connections (per-worker only).
   * Only tracks stats if enable_detailed_stats flag is true.
   * @param cluster_id the cluster identifier for the connection
   * @param success true for successful handshake, false for failure
   * @param failure_reason optional failure reason (e.g., "encode_error", "http.401", "http.500")
   */
  void incrementHandshakeStats(const std::string& cluster_id, bool success,
                               const std::string& failure_reason = "");

  /**
   * Test-only method to set the thread local slot for testing purposes.
   * This allows tests to inject a custom thread local registry and is used
   * in unit tests to simulate different worker threads.
   * @param slot the thread local slot to set
   */
  void setTestOnlyTLSRegistry(
      std::unique_ptr<ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>> slot) {
    tls_slot_ = std::move(slot);
  }

private:
  Server::Configuration::ServerFactoryContext& context_;
  const envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;
  ThreadLocal::TypedSlotPtr<DownstreamSocketThreadLocal> tls_slot_;
  std::string stat_prefix_; // Reverse connection stats prefix
  bool enable_detailed_stats_{false};
  std::string handshake_request_path_;
  std::vector<envoy::config::core::v3::HeaderValueOption> additional_headers_;
  bool use_http_upgrade_{false};
  AccessLog::InstanceSharedPtrVector access_logs_;

  /**
   * Update per-worker connection stats for debugging purposes.
   * Creates worker-specific stats. This is an internal function called only from
   * updateConnectionStats.
   * @param node_id the node identifier for the connection
   * @param cluster_id the cluster identifier for the connection
   * @param state_suffix the state suffix for the connection
   * @param increment whether to increment (true) or decrement (false) the connection count
   */
  void updatePerWorkerConnectionStats(const std::string& node_id, const std::string& cluster_id,
                                      const std::string& state_suffix, bool increment);
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

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
