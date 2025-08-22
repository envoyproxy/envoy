#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/random_generator.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class ReverseTunnelAcceptor;
class ReverseTunnelAcceptorExtension;
class UpstreamSocketManager;

/**
 * Custom IoHandle for upstream reverse connections that manages ConnectionSocket lifetime.
 * This class implements RAII principles to ensure proper socket cleanup and provides
 * reverse connection semantics where the connection is already established.
 */
class UpstreamReverseConnectionIOHandle : public Network::IoSocketHandleImpl {
public:
  /**
   * Constructs an UpstreamReverseConnectionIOHandle that takes ownership of a socket.
   *
   * @param socket the reverse connection socket to own and manage.
   * @param cluster_name the name of the cluster this connection belongs to.
   */
  UpstreamReverseConnectionIOHandle(Network::ConnectionSocketPtr socket,
                                    const std::string& cluster_name);

  ~UpstreamReverseConnectionIOHandle() override;

  // Network::IoHandle overrides
  /**
   * Override of connect method for reverse connections.
   * For reverse connections, the connection is already established so this method
   * is a no-op and always returns success.
   *
   * @param address the target address (unused for reverse connections).
   * @return SysCallIntResult with success status (0, 0).
   */
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;

  /**
   * Override of close method for reverse connections.
   * Cleans up the owned socket and calls the parent close method.
   *
   * @return IoCallUint64Result indicating the result of the close operation.
   */
  Api::IoCallUint64Result close() override;

  /**
   * Get the owned socket for read-only operations.
   *
   * @return const reference to the owned socket.
   */
  const Network::ConnectionSocket& getSocket() const { return *owned_socket_; }

private:
  // The name of the cluster this reverse connection belongs to.
  std::string cluster_name_;
  // The socket that this IOHandle owns and manages lifetime for.
  Network::ConnectionSocketPtr owned_socket_;
};

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
                            ReverseTunnelAcceptorExtension* extension = nullptr)
      : dispatcher_(dispatcher),
        socket_manager_(std::make_unique<UpstreamSocketManager>(dispatcher, extension)) {}

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
 * Socket interface that creates upstream reverse connection sockets.
 * Manages cached reverse TCP connections and provides them when requested.
 */
class ReverseTunnelAcceptor : public Envoy::Network::SocketInterfaceBase,
                              public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
public:
  /**
   * Constructs a ReverseTunnelAcceptor with the given server factory context.
   *
   * @param context the server factory context for this socket interface.
   */
  ReverseTunnelAcceptor(Server::Configuration::ServerFactoryContext& context);

  ReverseTunnelAcceptor() : extension_(nullptr), context_(nullptr) {}

  // SocketInterface overrides
  /**
   * Create a socket without a specific address (no-op for reverse connections).
   * @param socket_type the type of socket to create.
   * @param addr_type the address type.
   * @param version the IP version.
   * @param socket_v6only whether to create IPv6-only socket.
   * @param options socket creation options.
   * @return nullptr since reverse connections require specific addresses.
   */
  Envoy::Network::IoHandlePtr socket(Envoy::Network::Socket::Type, Envoy::Network::Address::Type,
                                     Envoy::Network::Address::IpVersion, bool,
                                     const Envoy::Network::SocketCreationOptions&) const override;

  /**
   * Create a socket with a specific address.
   * @param socket_type the type of socket to create.
   * @param addr the address to bind to.
   * @param options socket creation options.
   * @return IoHandlePtr for the reverse connection socket.
   */
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type,
         const Envoy::Network::Address::InstanceConstSharedPtr addr,
         const Envoy::Network::SocketCreationOptions& options) const override;

  /**
   * @param domain the IP family domain (AF_INET, AF_INET6).
   * @return true if the family is supported.
   */
  bool ipFamilySupported(int domain) override;

  /**
   * @return pointer to the thread-local registry, or nullptr if not available.
   */
  UpstreamSocketThreadLocal* getLocalRegistry() const;

  /**
   * Create a bootstrap extension for this socket interface.
   * @param config the config.
   * @param context the server factory context.
   * @return BootstrapExtensionPtr for the socket interface extension.
   */
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  /**
   * @return MessagePtr containing the empty configuration.
   */
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  /**
   * @return the interface name.
   */
  std::string name() const override {
    return "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface";
  }

  /**
   * @return pointer to the extension for cross-thread aggregation.
   */
  ReverseTunnelAcceptorExtension* getExtension() const { return extension_; }

  ReverseTunnelAcceptorExtension* extension_{nullptr};

private:
  Server::Configuration::ServerFactoryContext* context_;
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
   * @param sock_interface the socket interface to extend.
   * @param context the server factory context.
   * @param config the configuration for this extension.
   */
  ReverseTunnelAcceptorExtension(
      Envoy::Network::SocketInterface& sock_interface,
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
          UpstreamReverseConnectionSocketInterface& config)
      : Envoy::Network::SocketInterfaceExtension(sock_interface), context_(context),
        socket_interface_(static_cast<ReverseTunnelAcceptor*>(&sock_interface)) {
    ENVOY_LOG(debug,
              "ReverseTunnelAcceptorExtension: creating upstream reverse connection "
              "socket interface with stat_prefix: {}",
              stat_prefix_);
    stat_prefix_ =
        PROTOBUF_GET_STRING_OR_DEFAULT(config, stat_prefix, "upstream_reverse_connection");
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

  // RPING message now handled by ReverseConnectionUtility

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

  Event::TimerPtr ping_timer_;
  std::chrono::seconds ping_interval_{0};

  // Upstream extension for stats integration.
  ReverseTunnelAcceptorExtension* extension_;
};

DECLARE_FACTORY(ReverseTunnelAcceptor);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
