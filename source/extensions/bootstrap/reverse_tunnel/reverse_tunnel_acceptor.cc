#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"

#include <algorithm>
#include <atomic>
#include <future>
#include <thread>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/utility.h"
#include "source/common/reverse_connection/reverse_connection_utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// RPING message now handled by ReverseConnectionUtility

// UpstreamReverseConnectionIOHandle implementation
UpstreamReverseConnectionIOHandle::UpstreamReverseConnectionIOHandle(
    Network::ConnectionSocketPtr socket, const std::string& cluster_name)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), cluster_name_(cluster_name),
      owned_socket_(std::move(socket)) {

  ENVOY_LOG(debug, "Created UpstreamReverseConnectionIOHandle for cluster: {} with FD: {}",
            cluster_name_, fd_);
}

UpstreamReverseConnectionIOHandle::~UpstreamReverseConnectionIOHandle() {
  ENVOY_LOG(debug, "Destroying UpstreamReverseConnectionIOHandle for cluster: {} with FD: {}",
            cluster_name_, fd_);
  // The owned_socket_ will be automatically destroyed via RAII
}

Api::SysCallIntResult UpstreamReverseConnectionIOHandle::connect(
    Envoy::Network::Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(debug,
            "UpstreamReverseConnectionIOHandle::connect() to {} - connection already established "
            "through reverse tunnel",
            address->asString());

  // For reverse connections, the connection is already established.
  // We should return success immediately since the reverse tunnel provides the connection.
  return Api::SysCallIntResult{0, 0};
}

Api::IoCallUint64Result UpstreamReverseConnectionIOHandle::close() {
  ENVOY_LOG(debug, "UpstreamReverseConnectionIOHandle::close() called for FD: {}", fd_);

  // Reset the owned socket to properly close the connection
  // This ensures proper cleanup without requiring external storage
  if (owned_socket_) {
    ENVOY_LOG(debug, "Releasing owned socket for cluster: {}", cluster_name_);
    owned_socket_.reset();
  }

  // Call the parent close method
  return IoSocketHandleImpl::close();
}

// ReverseTunnelAcceptor implementation
ReverseTunnelAcceptor::ReverseTunnelAcceptor(Server::Configuration::ServerFactoryContext& context)
    : extension_(nullptr), context_(&context) {
  ENVOY_LOG(debug, "Created ReverseTunnelAcceptor.");
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptor::socket(Envoy::Network::Socket::Type socket_type,
                              Envoy::Network::Address::Type addr_type,
                              Envoy::Network::Address::IpVersion version, bool socket_v6only,
                              const Envoy::Network::SocketCreationOptions& options) const {

  (void)socket_type;
  (void)addr_type;
  (void)version;
  (void)socket_v6only;
  (void)options;

  ENVOY_LOG(warn, "ReverseTunnelAcceptor::socket() called without address - reverse "
                  "connections require specific addresses. Returning nullptr.");

  // Reverse connection sockets should always have an address (cluster ID)
  // This function should never be called for reverse connections
  return nullptr;
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptor::socket(Envoy::Network::Socket::Type socket_type,
                              const Envoy::Network::Address::InstanceConstSharedPtr addr,
                              const Envoy::Network::SocketCreationOptions& options) const {
  ENVOY_LOG(debug,
            "ReverseTunnelAcceptor::socket() called with address: {}. Finding socket for "
            "cluster/node: {}",
            addr->asString(), addr->logicalName());

  // For upstream reverse connections, we need to get the thread-local socket manager
  // and check if there are any cached connections available
  auto* tls_registry = getLocalRegistry();
  if (tls_registry && tls_registry->socketManager()) {
    auto* socket_manager = tls_registry->socketManager();

    // Get the cluster ID from the address's logical name
    std::string cluster_id = addr->logicalName();
    ENVOY_LOG(debug, "ReverseTunnelAcceptor: Using cluster ID from logicalName: {}", cluster_id);

    // Try to get a cached socket for the specific cluster
    auto [socket, expects_proxy_protocol] = socket_manager->getConnectionSocket(cluster_id);
    if (socket) {
      ENVOY_LOG(info, "Reusing cached reverse connection socket for cluster: {}", cluster_id);
      // Create IOHandle that properly owns the socket using RAII
      auto io_handle =
          std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(socket), cluster_id);
      return io_handle;
    }
  }

  ENVOY_LOG(debug, "No available reverse connection, falling back to standard socket");
  return Network::socketInterface(
             "envoy.extensions.network.socket_interface.default_socket_interface")
      ->socket(socket_type, addr, options);
}

bool ReverseTunnelAcceptor::ipFamilySupported(int domain) {
  // Support standard IP families.
  return domain == AF_INET || domain == AF_INET6;
}

// Get thread local registry for the current thread
UpstreamSocketThreadLocal* ReverseTunnelAcceptor::getLocalRegistry() const {
  if (extension_) {
    return extension_->getLocalRegistry();
  }
  return nullptr;
}

// BootstrapExtensionFactory
Server::BootstrapExtensionPtr ReverseTunnelAcceptor::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  ENVOY_LOG(debug, "ReverseTunnelAcceptor::createBootstrapExtension()");
  // Cast the config to the proper type
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
          UpstreamReverseConnectionSocketInterface&>(config, context.messageValidationVisitor());

  // Set the context for this socket interface instance
  context_ = &context;

  // Return a SocketInterfaceExtension that wraps this socket interface
  // The onServerInitialized() will be called automatically by the BootstrapExtension lifecycle
  return std::make_unique<ReverseTunnelAcceptorExtension>(*this, context, message);
}

ProtobufTypes::MessagePtr ReverseTunnelAcceptor::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
                              UpstreamReverseConnectionSocketInterface>();
}

// ReverseTunnelAcceptorExtension implementation
void ReverseTunnelAcceptorExtension::onServerInitialized() {
  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension::onServerInitialized - creating thread local slot");

  // Set the extension reference in the socket interface
  if (socket_interface_) {
    socket_interface_->extension_ = this;
  }

  // Create thread local slot to store dispatcher and socket manager for each worker thread
  tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(context_.threadLocal());

  // Set up the thread local dispatcher and socket manager for each worker thread
  tls_slot_->set([this](Event::Dispatcher& dispatcher) {
    return std::make_shared<UpstreamSocketThreadLocal>(dispatcher, context_.scope(), this);
  });
}

// Get thread local registry for the current thread
UpstreamSocketThreadLocal* ReverseTunnelAcceptorExtension::getLocalRegistry() const {
  ENVOY_LOG(debug, "ReverseTunnelAcceptorExtension::getLocalRegistry()");
  if (!tls_slot_) {
    ENVOY_LOG(debug, "ReverseTunnelAcceptorExtension::getLocalRegistry() - no thread local slot");
    return nullptr;
  }

  if (auto opt = tls_slot_->get(); opt.has_value()) {
    return &opt.value().get();
  }

  return nullptr;
}

absl::flat_hash_map<std::string, size_t>
ReverseTunnelAcceptorExtension::getAggregatedConnectionStats() {
  absl::flat_hash_map<std::string, size_t> aggregated_stats;

  if (!tls_slot_) {
    ENVOY_LOG(debug, "No TLS slot available for connection stats aggregation");
    return aggregated_stats;
  }

  // Get stats from current thread only - cross-thread aggregation in HTTP handler causes deadlock
  if (auto opt = tls_slot_->get(); opt.has_value() && opt->socketManager()) {
    auto thread_stats = opt->socketManager()->getConnectionStats();
    for (const auto& stat : thread_stats) {
      aggregated_stats[stat.first] = stat.second;
    }
    ENVOY_LOG(debug, "Got connection stats from current thread: {} nodes", aggregated_stats.size());
  } else {
    ENVOY_LOG(debug, "No socket manager available on current thread");
  }

  return aggregated_stats;
}

absl::flat_hash_map<std::string, size_t>
ReverseTunnelAcceptorExtension::getAggregatedSocketCountMap() {
  absl::flat_hash_map<std::string, size_t> aggregated_stats;

  if (!tls_slot_) {
    ENVOY_LOG(debug, "No TLS slot available for socket count aggregation");
    return aggregated_stats;
  }

  // Get stats from current thread only - cross-thread aggregation in HTTP handler causes deadlock
  if (auto opt = tls_slot_->get(); opt.has_value() && opt->socketManager()) {
    auto thread_stats = opt->socketManager()->getSocketCountMap();
    for (const auto& stat : thread_stats) {
      aggregated_stats[stat.first] = stat.second;
    }
    ENVOY_LOG(debug, "Got socket count from current thread: {} clusters", aggregated_stats.size());
  } else {
    ENVOY_LOG(debug, "No socket manager available on current thread");
  }

  return aggregated_stats;
}

void ReverseTunnelAcceptorExtension::getMultiTenantConnectionStats(
    std::function<void(const absl::flat_hash_map<std::string, size_t>&,
                       const std::vector<std::string>&)>
        callback) {

  if (!tls_slot_) {
    ENVOY_LOG(warn, "No TLS slot available for multi-tenant connection aggregation");
    callback({}, {});
    return;
  }

  // Create aggregation state - shared across all threads
  auto aggregation_state = std::make_shared<ConnectionAggregationState>();
  aggregation_state->completion_callback = std::move(callback);

  // Use Envoy's runOnAllThreads pattern for safe cross-thread data collection
  tls_slot_->runOnAllThreads(
      [aggregation_state](OptRef<UpstreamSocketThreadLocal> tls_instance) {
        absl::flat_hash_map<std::string, size_t> thread_stats;
        std::vector<std::string> thread_connected;
        std::vector<std::string> thread_accepted;

        if (tls_instance.has_value() && tls_instance->socketManager()) {
          // Collect connection stats from this thread
          auto connection_stats = tls_instance->socketManager()->getConnectionStats();
          for (const auto& [node_id, count] : connection_stats) {
            if (count > 0) {
              thread_connected.push_back(node_id);
              thread_stats[node_id] = count;
            }
          }

          // Collect accepted connections from this thread
          auto socket_count_map = tls_instance->socketManager()->getSocketCountMap();
          for (const auto& [cluster_id, count] : socket_count_map) {
            if (count > 0) {
              thread_accepted.push_back(cluster_id);
            }
          }
        }

        // Thread-safe aggregation
        {
          absl::MutexLock lock(&aggregation_state->mutex);

          // Merge connection stats
          for (const auto& [node_id, count] : thread_stats) {
            aggregation_state->connection_stats[node_id] += count;
          }

          // Merge connected nodes (de-duplicate)
          for (const auto& node : thread_connected) {
            if (std::find(aggregation_state->connected_nodes.begin(),
                          aggregation_state->connected_nodes.end(),
                          node) == aggregation_state->connected_nodes.end()) {
              aggregation_state->connected_nodes.push_back(node);
            }
          }

          // Merge accepted connections (de-duplicate)
          for (const auto& connection : thread_accepted) {
            if (std::find(aggregation_state->accepted_connections.begin(),
                          aggregation_state->accepted_connections.end(),
                          connection) == aggregation_state->accepted_connections.end()) {
              aggregation_state->accepted_connections.push_back(connection);
            }
          }
        }
      },
      [aggregation_state]() {
        // Completion callback - called when all threads have finished
        absl::MutexLock lock(&aggregation_state->mutex);
        if (!aggregation_state->completed) {
          aggregation_state->completed = true;
          ENVOY_LOG(debug,
                    "Multi-tenant connection aggregation completed: {} connection stats, {} "
                    "connected nodes, {} accepted connections",
                    aggregation_state->connection_stats.size(),
                    aggregation_state->connected_nodes.size(),
                    aggregation_state->accepted_connections.size());

          aggregation_state->completion_callback(aggregation_state->connection_stats,
                                                 aggregation_state->connected_nodes);
        }
      });
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ReverseTunnelAcceptorExtension::getConnectionStatsSync(std::chrono::milliseconds /* timeout_ms */) {

  ENVOY_LOG(debug, "getConnectionStatsSync: using stats-based approach for production reliability");

  // Use Envoy's stats system for reliable cross-thread aggregation
  auto connection_stats = getMultiTenantConnectionStatsViaStats();

  std::vector<std::string> connected_nodes;
  std::vector<std::string> accepted_connections;

  // Process the stats to extract connection information
  for (const auto& [stat_name, count] : connection_stats) {
    if (count > 0) {
      // Parse stat name to extract node/cluster information
      // Format: "reverse_connections.nodes.<node_id>" or
      // "reverse_connections.clusters.<cluster_id>"
      if (stat_name.find("reverse_connections.nodes.") == 0) {
        std::string node_id = stat_name.substr(strlen("reverse_connections.nodes."));
        connected_nodes.push_back(node_id);
      } else if (stat_name.find("reverse_connections.clusters.") == 0) {
        std::string cluster_id = stat_name.substr(strlen("reverse_connections.clusters."));
        accepted_connections.push_back(cluster_id);
      }
    }
  }

  ENVOY_LOG(debug, "getConnectionStatsSync: found {} connected nodes, {} accepted connections",
            connected_nodes.size(), accepted_connections.size());

  return {connected_nodes, accepted_connections};
}

absl::flat_hash_map<std::string, uint64_t>
ReverseTunnelAcceptorExtension::getMultiTenantConnectionStatsViaStats() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;

  // Use Envoy's proven stats aggregation - this automatically aggregates across all threads
  auto& stats_store = context_.scope();

  // Iterate through all gauges with the reverse_connections prefix using correct IterateFn
  // signature
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    if (gauge->name().find("reverse_connections.") == 0 && gauge->used()) {
      stats_map[gauge->name()] = gauge->value();
    }
    return true; // Continue iteration
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug,
            "getMultiTenantConnectionStatsViaStats: collected {} stats from Envoy's stats system",
            stats_map.size());

  return stats_map;
}

void ReverseTunnelAcceptorExtension::updateConnectionStatsRegistry(const std::string& node_id,
                                                                   const std::string& cluster_id,
                                                                   bool increment) {

  // Register stats with Envoy's system for automatic cross-thread aggregation
  auto& stats_store = context_.scope();

  // Create/update node connection stat
  if (!node_id.empty()) {
    std::string node_stat_name = fmt::format("reverse_connections.nodes.{}", node_id);
    auto& node_gauge =
        stats_store.gaugeFromString(node_stat_name, Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      node_gauge.inc();
      ENVOY_LOG(trace, "updateConnectionStatsRegistry: incremented node stat {} to {}",
                node_stat_name, node_gauge.value());
    } else {
      node_gauge.dec();
      ENVOY_LOG(trace, "updateConnectionStatsRegistry: decremented node stat {} to {}",
                node_stat_name, node_gauge.value());
    }
  }

  // Create/update cluster connection stat
  if (!cluster_id.empty()) {
    std::string cluster_stat_name = fmt::format("reverse_connections.clusters.{}", cluster_id);
    auto& cluster_gauge =
        stats_store.gaugeFromString(cluster_stat_name, Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      cluster_gauge.inc();
      ENVOY_LOG(trace, "updateConnectionStatsRegistry: incremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    } else {
      cluster_gauge.dec();
      ENVOY_LOG(trace, "updateConnectionStatsRegistry: decremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    }
  }
}

// UpstreamSocketManager implementation
UpstreamSocketManager::UpstreamSocketManager(Event::Dispatcher& dispatcher, Stats::Scope& scope,
                                             ReverseTunnelAcceptorExtension* extension)
    : dispatcher_(dispatcher), random_generator_(std::make_unique<Random::RandomGeneratorImpl>()),
      usm_scope_(scope.createScope("upstream_socket_manager.")), extension_(extension) {
  ENVOY_LOG(debug, "UpstreamSocketManager: creating UpstreamSocketManager with stats integration");
  ping_timer_ = dispatcher_.createTimer([this]() { pingConnections(); });
}

void UpstreamSocketManager::addConnectionSocket(const std::string& node_id,
                                                const std::string& cluster_id,
                                                Network::ConnectionSocketPtr socket,
                                                const std::chrono::seconds& ping_interval,
                                                bool rebalanced) {
  ENVOY_LOG(debug,
            "UpstreamSocketManager: addConnectionSocket called for node_id='{}' cluster_id='{}'",
            node_id, cluster_id);

  (void)rebalanced;
  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& connectionKey = socket->connectionInfoProvider().localAddress()->asString();

  ENVOY_LOG(debug, "UpstreamSocketManager: Adding connection socket for node: {} and cluster: {}",
            node_id, cluster_id);

  // Update stats for the node
  USMStats* node_stats = this->getStatsByNode(node_id);
  node_stats->reverse_conn_cx_total_.inc();
  node_stats->reverse_conn_cx_idle_.inc();
  ENVOY_LOG(debug, "UpstreamSocketManager: reverse conn count for node:{} idle: {} total:{}",
            node_id, node_stats->reverse_conn_cx_idle_.value(),
            node_stats->reverse_conn_cx_total_.value());

  ENVOY_LOG(debug,
            "UpstreamSocketManager: added socket to accepted_reverse_connections_ for node: {} "
            "cluster: {}",
            node_id, cluster_id);

  // Store node -> cluster mapping
  if (!cluster_id.empty()) {
    ENVOY_LOG(debug,
              "UpstreamSocketManager: adding node: {} cluster: {} to node_to_cluster_map_ and "
              "cluster_to_node_map_",
              node_id, cluster_id);
    if (node_to_cluster_map_.find(node_id) == node_to_cluster_map_.end()) {
      node_to_cluster_map_[node_id] = cluster_id;
      cluster_to_node_map_[cluster_id].push_back(node_id);
    }
    ENVOY_LOG(debug, "UpstreamSocketManager: node_to_cluster_map_ size: {}",
              node_to_cluster_map_.size());
    ENVOY_LOG(debug, "UpstreamSocketManager: cluster_to_node_map_ size: {}",
              cluster_to_node_map_.size());
    // Update stats for the cluster
    USMStats* cluster_stats = this->getStatsByCluster(cluster_id);
    cluster_stats->reverse_conn_cx_total_.inc();
    cluster_stats->reverse_conn_cx_idle_.inc();
  } else {
    ENVOY_LOG(error, "Found a reverse connection with an empty cluster uuid, and node uuid: {}",
              node_id);
  }

  // If local envoy is responding to reverse connections, add the socket to
  // accepted_reverse_connections_. Thereafter, initiate ping keepalives on the socket.
  accepted_reverse_connections_[node_id].push_back(std::move(socket));
  Network::ConnectionSocketPtr& socket_ref = accepted_reverse_connections_[node_id].back();

  ENVOY_LOG(debug, "UpstreamSocketManager: mapping fd {} to node '{}'", fd, node_id);
  fd_to_node_map_[fd] = node_id;

  // Update Envoy's stats system for production multi-tenant tracking
  // This integrates with Envoy's proven cross-thread stats aggregation
  if (auto extension = getUpstreamExtension()) {
    extension->updateConnectionStatsRegistry(node_id, cluster_id, true /* increment */);
    ENVOY_LOG(debug, "UpstreamSocketManager: updated stats registry for node '{}' cluster '{}'",
              node_id, cluster_id);
  }

  // onPingResponse() expects a ping reply on the socket.
  fd_to_event_map_[fd] = dispatcher_.createFileEvent(
      fd,
      [this, &socket_ref](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        onPingResponse(socket_ref->ioHandle());
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);

  fd_to_timer_map_[fd] =
      dispatcher_.createTimer([this, fd]() { markSocketDead(fd, false /* used */); });

  // Initiate ping keepalives on the socket.
  tryEnablePingTimer(std::chrono::seconds(ping_interval.count()));

  ENVOY_LOG(
      info,
      "UpstreamSocketManager: done adding socket to maps with node: {} connection key: {} fd: {}",
      node_id, connectionKey, fd);
}

std::pair<Network::ConnectionSocketPtr, bool>
UpstreamSocketManager::getConnectionSocket(const std::string& key) {

  ENVOY_LOG(debug, "UpstreamSocketManager: getConnectionSocket() called with key: {}", key);
  // The key can be cluster_id or node_id. If any worker has a socket for the key, treat it as a
  // cluster ID. Otherwise treat it as a node ID.
  std::string node_id = key;
  std::string actual_cluster_id = "";

  // If we have sockets for this key as a cluster ID, treat it as a cluster
  if (getNumberOfSocketsByCluster(key) > 0) {
    actual_cluster_id = key;
    auto cluster_nodes_it = cluster_to_node_map_.find(actual_cluster_id);
    if (cluster_nodes_it != cluster_to_node_map_.end() && !cluster_nodes_it->second.empty()) {
      // Pick a random node for the cluster
      auto node_idx = random_generator_->random() % cluster_nodes_it->second.size();
      node_id = cluster_nodes_it->second[node_idx];
    } else {
      ENVOY_LOG(debug, "UpstreamSocketManager: No nodes found for cluster: {}", actual_cluster_id);
      return {nullptr, false};
    }
  }

  ENVOY_LOG(debug, "UpstreamSocketManager: Looking for socket with node: {} cluster: {}", node_id,
            actual_cluster_id);

  // Find first available socket for the node
  auto node_sockets_it = accepted_reverse_connections_.find(node_id);
  if (node_sockets_it == accepted_reverse_connections_.end() || node_sockets_it->second.empty()) {
    ENVOY_LOG(debug, "UpstreamSocketManager: No available sockets for node: {}", node_id);
    return {nullptr, false};
  }

  // Fetch the socket from the accepted_reverse_connections_ and remove it from the list
  Network::ConnectionSocketPtr socket(std::move(node_sockets_it->second.front()));
  node_sockets_it->second.pop_front();

  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& remoteConnectionKey =
      socket->connectionInfoProvider().remoteAddress()->asString();

  ENVOY_LOG(debug,
            "UpstreamSocketManager: Reverse conn socket with FD:{} connection key:{} found for "
            "node: {} and "
            "cluster: {}",
            fd, remoteConnectionKey, node_id, actual_cluster_id);

  fd_to_node_map_.erase(fd);
  fd_to_event_map_.erase(fd);
  fd_to_timer_map_.erase(fd);

  cleanStaleNodeEntry(node_id);

  // Update stats
  USMStats* node_stats = this->getStatsByNode(node_id);
  node_stats->reverse_conn_cx_idle_.dec();
  node_stats->reverse_conn_cx_used_.inc();

  if (!actual_cluster_id.empty()) {
    USMStats* cluster_stats = this->getStatsByCluster(actual_cluster_id);
    cluster_stats->reverse_conn_cx_idle_.dec();
    cluster_stats->reverse_conn_cx_used_.inc();
  }

  return {std::move(socket), false};
}

size_t UpstreamSocketManager::getNumberOfSocketsByCluster(const std::string& cluster_id) {
  USMStats* stats = this->getStatsByCluster(cluster_id);
  if (!stats) {
    ENVOY_LOG(error, "UpstreamSocketManager: No stats available for cluster: {}", cluster_id);
    return 0;
  }
  ENVOY_LOG(debug, "UpstreamSocketManager: Number of sockets for cluster: {} is {}", cluster_id,
            stats->reverse_conn_cx_idle_.value());
  return stats->reverse_conn_cx_idle_.value();
}

size_t UpstreamSocketManager::getNumberOfSocketsByNode(const std::string& node_id) {
  USMStats* stats = this->getStatsByNode(node_id);
  if (!stats) {
    ENVOY_LOG(error, "UpstreamSocketManager: No stats available for node: {}", node_id);
    return 0;
  }
  ENVOY_LOG(debug, "UpstreamSocketManager: Number of sockets for node: {} is {}", node_id,
            stats->reverse_conn_cx_idle_.value());
  return stats->reverse_conn_cx_idle_.value();
}

bool UpstreamSocketManager::deleteStatsByNode(const std::string& node_id) {
  const auto& iter = usm_node_stats_map_.find(node_id);
  if (iter == usm_node_stats_map_.end()) {
    return false;
  }
  usm_node_stats_map_.erase(iter);
  return true;
}

bool UpstreamSocketManager::deleteStatsByCluster(const std::string& cluster_id) {
  const auto& iter = usm_cluster_stats_map_.find(cluster_id);
  if (iter == usm_cluster_stats_map_.end()) {
    return false;
  }
  usm_cluster_stats_map_.erase(iter);
  return true;
}

absl::flat_hash_map<std::string, size_t> UpstreamSocketManager::getConnectionStats() {
  absl::flat_hash_map<std::string, size_t> node_stats;
  for (const auto& node_entry : accepted_reverse_connections_) {
    const std::string& node_id = node_entry.first;
    size_t connection_count = node_entry.second.size();
    if (connection_count > 0) {
      node_stats[node_id] = connection_count;
    }
  }
  ENVOY_LOG(debug, "UpstreamSocketManager::getConnectionStats returning {} nodes",
            node_stats.size());
  return node_stats;
}

absl::flat_hash_map<std::string, size_t> UpstreamSocketManager::getSocketCountMap() {
  absl::flat_hash_map<std::string, size_t> cluster_stats;
  for (const auto& cluster_entry : cluster_to_node_map_) {
    const std::string& cluster_id = cluster_entry.first;
    size_t total_connections = 0;

    // Sum up connections for all nodes in this cluster
    for (const std::string& node_id : cluster_entry.second) {
      const auto& node_conn_iter = accepted_reverse_connections_.find(node_id);
      if (node_conn_iter != accepted_reverse_connections_.end()) {
        total_connections += node_conn_iter->second.size();
      }
    }

    if (total_connections > 0) {
      cluster_stats[cluster_id] = total_connections;
    }
  }
  ENVOY_LOG(debug, "UpstreamSocketManager::getSocketCountMap returning {} clusters",
            cluster_stats.size());
  return cluster_stats;
}

void UpstreamSocketManager::markSocketDead(const int fd, const bool used) {
  ENVOY_LOG(debug, "UpstreamSocketManager: markSocketDead called for fd {}", fd);

  auto node_it = fd_to_node_map_.find(fd);
  if (node_it == fd_to_node_map_.end()) {
    ENVOY_LOG(debug, "UpstreamSocketManager: FD {} not found in fd_to_node_map_", fd);
    return;
  }

  const std::string node_id = node_it->second; // Make a COPY, not a reference
  ENVOY_LOG(debug, "UpstreamSocketManager: found node '{}' for fd {}", node_id, fd);

  std::string cluster_id = (node_to_cluster_map_.find(node_id) != node_to_cluster_map_.end())
                               ? node_to_cluster_map_[node_id]
                               : "";
  fd_to_node_map_.erase(fd); // Now it's safe to erase since node_id is a copy

  // If this is a used connection, we update the stats and return.
  if (used) {
    ENVOY_LOG(debug, "UpstreamSocketManager: Marking used socket dead. node: {} cluster: {} FD: {}",
              node_id, cluster_id, fd);
    USMStats* stats = this->getStatsByNode(node_id);
    if (stats) {
      stats->reverse_conn_cx_used_.dec();
      stats->reverse_conn_cx_total_.dec();
    }
    return;
  }

  auto& sockets = accepted_reverse_connections_[node_id];
  bool socket_found = false;
  for (auto itr = sockets.begin(); itr != sockets.end(); itr++) {
    if (fd == itr->get()->ioHandle().fdDoNotUse()) {
      ENVOY_LOG(debug, "UpstreamSocketManager: Marking socket dead; node: {}, cluster: {} FD: {}",
                node_id, cluster_id, fd);
      ::shutdown(fd, SHUT_RDWR);
      itr = sockets.erase(itr);
      socket_found = true;

      fd_to_event_map_.erase(fd);
      fd_to_timer_map_.erase(fd);

      // Update stats
      USMStats* node_stats = this->getStatsByNode(node_id);
      if (node_stats) {
        node_stats->reverse_conn_cx_idle_.dec();
        node_stats->reverse_conn_cx_total_.dec();
      }

      if (!cluster_id.empty()) {
        USMStats* cluster_stats = this->getStatsByCluster(cluster_id);
        if (cluster_stats) {
          cluster_stats->reverse_conn_cx_idle_.dec();
          cluster_stats->reverse_conn_cx_total_.dec();
        }
      }

      // Update Envoy's stats system for production multi-tenant tracking
      // This ensures stats are decremented when connections are removed
      if (auto extension = getUpstreamExtension()) {
        extension->updateConnectionStatsRegistry(node_id, cluster_id, false /* decrement */);
        ENVOY_LOG(debug,
                  "UpstreamSocketManager: decremented stats registry for node '{}' cluster '{}'",
                  node_id, cluster_id);
      }
      break;
    }
  }

  if (!socket_found) {
    ENVOY_LOG(error, "UpstreamSocketManager: Marking an invalid socket dead. node: {} FD: {}",
              node_id, fd);
  }

  if (sockets.size() == 0) {
    cleanStaleNodeEntry(node_id);
  }
}

void UpstreamSocketManager::tryEnablePingTimer(const std::chrono::seconds& ping_interval) {
  ENVOY_LOG(debug, "UpstreamSocketManager: trying to enable ping timer, ping interval: {}",
            ping_interval.count());
  if (ping_interval_ != std::chrono::seconds::zero()) {
    return;
  }
  ENVOY_LOG(debug, "UpstreamSocketManager: enabling ping timer, ping interval: {}",
            ping_interval.count());
  ping_interval_ = ping_interval;
  ping_timer_->enableTimer(ping_interval_);
}

void UpstreamSocketManager::cleanStaleNodeEntry(const std::string& node_id) {
  // Clean the given node-id, if there are no active sockets.
  if (accepted_reverse_connections_.find(node_id) != accepted_reverse_connections_.end() &&
      accepted_reverse_connections_[node_id].size() > 0) {
    ENVOY_LOG(debug, "Found {} active sockets for node: {}",
              accepted_reverse_connections_[node_id].size(), node_id);
    return;
  }
  ENVOY_LOG(debug, "UpstreamSocketManager: Cleaning stale node entry for node: {}", node_id);

  // Check if given node-id, is present in node_to_cluster_map_. If present,
  // fetch the corresponding cluster-id. Use cluster-id and node-id to delete entry
  // from cluster_to_node_map_ and node_to_cluster_map_ respectively.
  const auto& node_itr = node_to_cluster_map_.find(node_id);
  if (node_itr != node_to_cluster_map_.end()) {
    const auto& cluster_itr = cluster_to_node_map_.find(node_itr->second);
    if (cluster_itr != cluster_to_node_map_.end()) {
      const auto& node_entry_itr =
          find(cluster_itr->second.begin(), cluster_itr->second.end(), node_id);

      if (node_entry_itr != cluster_itr->second.end()) {
        ENVOY_LOG(debug, "UpstreamSocketManager:Removing stale node {} from cluster {}", node_id,
                  cluster_itr->first);
        cluster_itr->second.erase(node_entry_itr);

        // If the cluster to node-list map has an empty vector, remove
        // the entry from map.
        if (cluster_itr->second.size() == 0) {
          cluster_to_node_map_.erase(cluster_itr);
        }
      }
    }
    node_to_cluster_map_.erase(node_itr);
  }
}

void UpstreamSocketManager::onPingResponse(Network::IoHandle& io_handle) {
  const int fd = io_handle.fdDoNotUse();

  Buffer::OwnedImpl buffer;
  const auto ping_size = ::Envoy::ReverseConnection::ReverseConnectionUtility::PING_MESSAGE.size();
  Api::IoCallUint64Result result = io_handle.read(buffer, absl::make_optional(ping_size));
  if (!result.ok()) {
    ENVOY_LOG(debug, "UpstreamSocketManager: Read error on FD: {}: error - {}", fd,
              result.err_->getErrorDetails());
    markSocketDead(fd, false /* used */);
    return;
  }

  // In this case, there is no read error, but the socket has been closed by the remote
  // peer in a graceful manner, unlike a connection refused, or a reset.
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "UpstreamSocketManager: FD: {}: reverse connection closed", fd);
    markSocketDead(fd, false /* used */);
    return;
  }

  if (result.return_value_ < ping_size) {
    ENVOY_LOG(debug, "UpstreamSocketManager: FD: {}: no complete ping data yet", fd);
    return;
  }

  if (!::Envoy::ReverseConnection::ReverseConnectionUtility::isPingMessage(buffer.toString())) {
    ENVOY_LOG(debug, "UpstreamSocketManager: FD: {}: response is not RPING", fd);
    markSocketDead(fd, false /* used */);
    return;
  }
  ENVOY_LOG(trace, "UpstreamSocketManager: FD: {}: received ping response", fd);
  fd_to_timer_map_[fd]->disableTimer();
}

void UpstreamSocketManager::pingConnections(const std::string& node_id) {
  ENVOY_LOG(debug, "UpstreamSocketManager: Pinging connections for node: {}", node_id);
  auto& sockets = accepted_reverse_connections_[node_id];
  ENVOY_LOG(debug, "UpstreamSocketManager: node:{} Number of sockets:{}", node_id, sockets.size());
  for (auto itr = sockets.begin(); itr != sockets.end(); itr++) {
    int fd = itr->get()->ioHandle().fdDoNotUse();
    auto buffer = ::Envoy::ReverseConnection::ReverseConnectionUtility::createPingResponse();

    auto ping_response_timeout = ping_interval_ / 2;
    fd_to_timer_map_[fd]->enableTimer(ping_response_timeout);
    while (buffer->length() > 0) {
      Api::IoCallUint64Result result = itr->get()->ioHandle().write(*buffer);
      ENVOY_LOG(trace,
                "UpstreamSocketManager: node:{} FD:{}: sending ping request. return_value: {}",
                node_id, fd, result.return_value_);
      if (result.return_value_ == 0) {
        ENVOY_LOG(debug, "UpstreamSocketManager: node:{} FD:{}: sending ping rc {}, error - ",
                  node_id, fd, result.return_value_, result.err_->getErrorDetails());
        if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          ENVOY_LOG(debug, "UpstreamSocketManager: node:{} FD:{}: failed to send ping", node_id,
                    fd);
          ::shutdown(fd, SHUT_RDWR);
          sockets.erase(itr--);
          cleanStaleNodeEntry(node_id);
          break;
        }
      }
    }

    if (buffer->length() > 0) {
      continue;
    }
  }
}

void UpstreamSocketManager::pingConnections() {
  ENVOY_LOG(trace, "UpstreamSocketManager: Pinging connections");
  for (auto& itr : accepted_reverse_connections_) {
    pingConnections(itr.first);
  }
  ping_timer_->enableTimer(ping_interval_);
}

USMStats* UpstreamSocketManager::getStatsByNode(const std::string& node_id) {
  auto iter = usm_node_stats_map_.find(node_id);
  if (iter != usm_node_stats_map_.end()) {
    USMStats* stats = iter->second.get();
    return stats;
  }

  ENVOY_LOG(debug, "UpstreamSocketManager: Creating new stats for node: {}", node_id);
  const std::string& final_prefix = "node." + node_id;
  usm_node_stats_map_[node_id] = std::make_unique<USMStats>(
      USMStats{ALL_USM_STATS(POOL_GAUGE_PREFIX(*usm_scope_, final_prefix))});
  return usm_node_stats_map_[node_id].get();
}

USMStats* UpstreamSocketManager::getStatsByCluster(const std::string& cluster_id) {
  auto iter = usm_cluster_stats_map_.find(cluster_id);
  if (iter != usm_cluster_stats_map_.end()) {
    USMStats* stats = iter->second.get();
    return stats;
  }

  ENVOY_LOG(debug, "UpstreamSocketManager: Creating new stats for cluster: {}", cluster_id);
  const std::string& final_prefix = "cluster." + cluster_id;
  usm_cluster_stats_map_[cluster_id] = std::make_unique<USMStats>(
      USMStats{ALL_USM_STATS(POOL_GAUGE_PREFIX(*usm_scope_, final_prefix))});
  return usm_cluster_stats_map_[cluster_id].get();
}

REGISTER_FACTORY(ReverseTunnelAcceptor, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
