#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

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

  void onServerInitialized() override;
  void onWorkerThreadInitialized() override;

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
   * Update per-worker connection stats for debugging purposes.
   * Creates worker-specific stats
   * @param node_id the node identifier for the connection
   * @param cluster_id the cluster identifier for the connection
   * @param state_suffix the state suffix for the connection
   * @param increment whether to increment (true) or decrement (false) the connection count
   */
  void updatePerWorkerConnectionStats(const std::string& node_id, const std::string& cluster_id,
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
