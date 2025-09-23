#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class UpstreamSocketManager;
class ReverseTunnelAcceptorExtension;

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
  // Friend class for testing
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
          UpstreamReverseConnectionSocketInterface& config)
      : Envoy::Network::SocketInterfaceExtension(sock_interface), context_(context),
        socket_interface_(&sock_interface) {
    ENVOY_LOG(debug,
              "ReverseTunnelAcceptorExtension: creating upstream reverse connection "
              "socket interface with stat_prefix: {}",
              stat_prefix_);
    stat_prefix_ =
        PROTOBUF_GET_STRING_OR_DEFAULT(config, stat_prefix, "upstream_reverse_connection");
    // Ensure the socket interface has a reference to this extension early, so stats can be
    // recorded even before onServerInitialized().
    if (socket_interface_ != nullptr) {
      socket_interface_->extension_ = this;
    }
  }

  /**
   * Called when the server is initialized.
   */
  void onServerInitialized() override;

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
                             bool increment);

  /**
   * Update per-worker connection stats for debugging.
   * @param node_id the node identifier for the connection.
   * @param cluster_id the cluster identifier for the connection.
   * @param increment whether to increment (true) or decrement (false) the connection count.
   */
  void updatePerWorkerConnectionStats(const std::string& node_id, const std::string& cluster_id,
                                      bool increment);

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
   * Test-only method to set the thread local slot.
   * @param slot the thread local slot to set.
   */
  void
  setTestOnlyTLSRegistry(std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> slot) {
    tls_slot_ = std::move(slot);
  }

private:
  Server::Configuration::ServerFactoryContext& context_;
  // Thread-local slot for storing the socket manager per worker thread.
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  ReverseTunnelAcceptor* socket_interface_;
  std::string stat_prefix_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
