#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"

#include <algorithm>
#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_connection_utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

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
            "node: {}",
            addr->asString(), addr->logicalName());

  // For upstream reverse connections, we need to get the thread-local socket manager
  // and check if there are any cached connections available
  auto* tls_registry = getLocalRegistry();
  if (tls_registry && tls_registry->socketManager()) {
    auto* socket_manager = tls_registry->socketManager();

    // The address's logical name should already be the node ID
    std::string node_id = addr->logicalName();
    ENVOY_LOG(debug, "ReverseTunnelAcceptor: Using node_id from logicalName: {}", node_id);

    // Try to get a cached socket for the specific node
    auto socket = socket_manager->getConnectionSocket(node_id);
    if (socket) {
      ENVOY_LOG(info, "Reusing cached reverse connection socket for node: {}", node_id);
      // Create IOHandle that properly owns the socket using RAII
      auto io_handle =
          std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(socket), node_id);
      return io_handle;
    }
  }

  // This is unexpected. This indicates that no sockets are available for the node.
  // Fallback to standard socket interface.
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
    return std::make_shared<UpstreamSocketThreadLocal>(dispatcher, this);
  });
}

// Get thread local registry for the current thread
UpstreamSocketThreadLocal* ReverseTunnelAcceptorExtension::getLocalRegistry() const {
  ENVOY_LOG(debug, "ReverseTunnelAcceptorExtension::getLocalRegistry()");
  if (!tls_slot_) {
    ENVOY_LOG(warn, "ReverseTunnelAcceptorExtension::getLocalRegistry() - no thread local slot");
    return nullptr;
  }

  if (auto opt = tls_slot_->get(); opt.has_value()) {
    return &opt.value().get();
  }

  return nullptr;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ReverseTunnelAcceptorExtension::getConnectionStatsSync(std::chrono::milliseconds /* timeout_ms */) {

  ENVOY_LOG(debug, "ReverseTunnelAcceptorExtension: obtaining reverse connection stats");

  // Get all gauges with the reverse_connections prefix.
  auto connection_stats = getCrossWorkerStatMap();

  std::vector<std::string> connected_nodes;
  std::vector<std::string> accepted_connections;

  // Process the stats to extract connection information
  for (const auto& [stat_name, count] : connection_stats) {
    if (count > 0) {
      // Parse stat name to extract node/cluster information
      // Format: "<scope>.reverse_connections.nodes.<node_id>" or
      // "<scope>.reverse_connections.clusters.<cluster_id>"
      if (stat_name.find("reverse_connections.nodes.") != std::string::npos) {
        // Find the position after "reverse_connections.nodes."
        size_t pos = stat_name.find("reverse_connections.nodes.");
        if (pos != std::string::npos) {
          std::string node_id = stat_name.substr(pos + strlen("reverse_connections.nodes."));
          connected_nodes.push_back(node_id);
        }
      } else if (stat_name.find("reverse_connections.clusters.") != std::string::npos) {
        // Find the position after "reverse_connections.clusters."
        size_t pos = stat_name.find("reverse_connections.clusters.");
        if (pos != std::string::npos) {
          std::string cluster_id = stat_name.substr(pos + strlen("reverse_connections.clusters."));
          accepted_connections.push_back(cluster_id);
        }
      }
    }
  }

  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension: found {} connected nodes, {} accepted connections",
            connected_nodes.size(), accepted_connections.size());

  return {connected_nodes, accepted_connections};
}

absl::flat_hash_map<std::string, uint64_t> ReverseTunnelAcceptorExtension::getCrossWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Iterate through all gauges and filter for cross-worker stats only.
  // Cross-worker stats have the pattern "reverse_connections.nodes.<node_id>" or
  // "reverse_connections.clusters.<cluster_id>" (no dispatcher name in the middle).
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find("reverse_connections.") != std::string::npos &&
        (gauge_name.find("reverse_connections.nodes.") != std::string::npos ||
         gauge_name.find("reverse_connections.clusters.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension: collected {} stats for reverse connections across all "
            "worker threads",
            stats_map.size());

  return stats_map;
}

void ReverseTunnelAcceptorExtension::updateConnectionStats(const std::string& node_id,
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
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented node stat {} to {}",
                node_stat_name, node_gauge.value());
    } else {
      node_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented node stat {} to {}",
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
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    } else {
      cluster_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    }
  }

  // Also update per-worker stats for debugging
  updatePerWorkerConnectionStats(node_id, cluster_id, increment);
}

void ReverseTunnelAcceptorExtension::updatePerWorkerConnectionStats(const std::string& node_id,
                                                                    const std::string& cluster_id,
                                                                    bool increment) {
  auto& stats_store = context_.scope();

  // Get dispatcher name from the thread local dispatcher
  std::string dispatcher_name = "main_thread"; // Default for main thread
  auto* local_registry = getLocalRegistry();
  if (local_registry) {
    // Dispatcher name is of the form "worker_x" where x is the worker index
    dispatcher_name = local_registry->dispatcher().name();
  }

  // Create/update per-worker node connection stat
  if (!node_id.empty()) {
    std::string worker_node_stat_name =
        fmt::format("reverse_connections.{}.node.{}", dispatcher_name, node_id);
    auto& worker_node_gauge =
        stats_store.gaugeFromString(worker_node_stat_name, Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_node_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented worker node stat {} to {}",
                worker_node_stat_name, worker_node_gauge.value());
    } else {
      worker_node_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented worker node stat {} to {}",
                worker_node_stat_name, worker_node_gauge.value());
    }
  }

  // Create/update per-worker cluster connection stat
  if (!cluster_id.empty()) {
    std::string worker_cluster_stat_name =
        fmt::format("reverse_connections.{}.cluster.{}", dispatcher_name, cluster_id);
    auto& worker_cluster_gauge = stats_store.gaugeFromString(worker_cluster_stat_name,
                                                             Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_cluster_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    } else {
      worker_cluster_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    }
  }
}

absl::flat_hash_map<std::string, uint64_t> ReverseTunnelAcceptorExtension::getPerWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Get the current dispatcher name
  std::string dispatcher_name = "main_thread"; // Default for main thread
  auto* local_registry = getLocalRegistry();
  if (local_registry) {
    // Dispatcher name is of the form "worker_x" where x is the worker index
    dispatcher_name = local_registry->dispatcher().name();
  }

  // Iterate through all gauges and filter for the current dispatcher
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map, &dispatcher_name](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find("reverse_connections.") != std::string::npos &&
        gauge_name.find(dispatcher_name + ".") != std::string::npos &&
        (gauge_name.find(".node.") != std::string::npos ||
         gauge_name.find(".cluster.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug, "ReverseTunnelAcceptorExtension: collected {} stats for dispatcher '{}'",
            stats_map.size(), dispatcher_name);

  return stats_map;
}

// UpstreamSocketManager implementation
UpstreamSocketManager::UpstreamSocketManager(Event::Dispatcher& dispatcher,
                                             ReverseTunnelAcceptorExtension* extension)
    : dispatcher_(dispatcher), random_generator_(std::make_unique<Random::RandomGeneratorImpl>()),
      extension_(extension) {
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

  // Both node_id and cluster_id are mandatory for consistent state management and stats tracking
  if (node_id.empty() || cluster_id.empty()) {
    ENVOY_LOG(error,
              "UpstreamSocketManager: addConnectionSocket called with empty node_id='{}' or "
              "cluster_id='{}'. Both are mandatory.",
              node_id, cluster_id);
    return;
  }

  (void)rebalanced;
  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& connectionKey = socket->connectionInfoProvider().localAddress()->asString();

  ENVOY_LOG(debug, "UpstreamSocketManager: Adding connection socket for node: {} and cluster: {}",
            node_id, cluster_id);

  // Store node -> cluster mapping
  ENVOY_LOG(trace,
            "UpstreamSocketManager: adding node: {} cluster: {} to node_to_cluster_map_ and "
            "cluster_to_node_map_",
            node_id, cluster_id);
  if (node_to_cluster_map_.find(node_id) == node_to_cluster_map_.end()) {
    node_to_cluster_map_[node_id] = cluster_id;
    cluster_to_node_map_[cluster_id].push_back(node_id);
  }
  ENVOY_LOG(trace,
            "UpstreamSocketManager: node_to_cluster_map_ has {} entries, cluster_to_node_map_ has "
            "{} entries",
            node_to_cluster_map_.size(), cluster_to_node_map_.size());

  ENVOY_LOG(trace,
            "UpstreamSocketManager: added socket to accepted_reverse_connections_ for node: {} "
            "cluster: {}",
            node_id, cluster_id);

  // If local envoy is responding to reverse connections, add the socket to
  // accepted_reverse_connections_. Thereafter, initiate ping keepalives on the socket.
  accepted_reverse_connections_[node_id].push_back(std::move(socket));
  Network::ConnectionSocketPtr& socket_ref = accepted_reverse_connections_[node_id].back();

  ENVOY_LOG(debug, "UpstreamSocketManager: mapping fd {} to node '{}'", fd, node_id);
  fd_to_node_map_[fd] = node_id;

  // Update stats registry
  if (auto extension = getUpstreamExtension()) {
    extension->updateConnectionStats(node_id, cluster_id, true /* increment */);
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

  fd_to_timer_map_[fd] = dispatcher_.createTimer([this, fd]() { markSocketDead(fd); });

  // Initiate ping keepalives on the socket.
  tryEnablePingTimer(std::chrono::seconds(ping_interval.count()));

  ENVOY_LOG(
      info,
      "UpstreamSocketManager: done adding socket to maps with node: {} connection key: {} fd: {}",
      node_id, connectionKey, fd);
}

Network::ConnectionSocketPtr
UpstreamSocketManager::getConnectionSocket(const std::string& node_id) {

  ENVOY_LOG(debug, "UpstreamSocketManager: getConnectionSocket() called with node_id: {}", node_id);

  if (node_to_cluster_map_.find(node_id) == node_to_cluster_map_.end()) {
    ENVOY_LOG(error, "UpstreamSocketManager: cluster -> node mapping changed for node: {}",
              node_id);
    return nullptr;
  }

  const std::string& cluster_id = node_to_cluster_map_[node_id];

  ENVOY_LOG(debug, "UpstreamSocketManager: Looking for socket with node: {} cluster: {}", node_id,
            cluster_id);

  // Find first available socket for the node
  auto node_sockets_it = accepted_reverse_connections_.find(node_id);
  if (node_sockets_it == accepted_reverse_connections_.end() || node_sockets_it->second.empty()) {
    ENVOY_LOG(debug, "UpstreamSocketManager: No available sockets for node: {}", node_id);
    return nullptr;
  }

  // Debugging: Print the number of free sockets on this worker thread
  ENVOY_LOG(debug, "UpstreamSocketManager: Found {} sockets for node: {}",
            node_sockets_it->second.size(), node_id);

  // Fetch the socket from the accepted_reverse_connections_ and remove it from the list
  Network::ConnectionSocketPtr socket(std::move(node_sockets_it->second.front()));
  node_sockets_it->second.pop_front();

  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& remoteConnectionKey =
      socket->connectionInfoProvider().remoteAddress()->asString();

  ENVOY_LOG(debug,
            "UpstreamSocketManager: Reverse conn socket with FD:{} connection key:{} found for "
            "node: {} cluster: {}",
            fd, remoteConnectionKey, node_id, cluster_id);

  fd_to_event_map_.erase(fd);
  fd_to_timer_map_.erase(fd);

  cleanStaleNodeEntry(node_id);

  return socket;
}

std::string UpstreamSocketManager::getNodeID(const std::string& key) {
  ENVOY_LOG(debug, "UpstreamSocketManager: getNodeID() called with key: {}", key);

  // First check if the key exists as a cluster ID by checking global stats
  // This ensures we check across all threads, not just the current thread
  if (auto extension = getUpstreamExtension()) {
    // Check if any thread has sockets for this cluster by looking at global stats
    std::string cluster_stat_name = fmt::format("reverse_connections.clusters.{}", key);
    auto& stats_store = extension->getStatsScope();
    auto& cluster_gauge =
        stats_store.gaugeFromString(cluster_stat_name, Stats::Gauge::ImportMode::Accumulate);

    if (cluster_gauge.value() > 0) {
      // Key is a cluster ID with active connections, find a node from this cluster
      auto cluster_nodes_it = cluster_to_node_map_.find(key);
      if (cluster_nodes_it != cluster_to_node_map_.end() && !cluster_nodes_it->second.empty()) {
        // Return a random existing node from this cluster
        auto node_idx = random_generator_->random() % cluster_nodes_it->second.size();
        std::string node_id = cluster_nodes_it->second[node_idx];
        ENVOY_LOG(debug,
                  "UpstreamSocketManager: key '{}' is cluster ID with {} connections, returning "
                  "random node: {}",
                  key, cluster_gauge.value(), node_id);
        return node_id;
      }
      // If cluster has connections but no local mapping, assume key is a node ID
    }
  }

  // Key is not a cluster ID, has no connections, or has no local mapping
  // Treat it as a node ID and return it directly
  ENVOY_LOG(debug, "UpstreamSocketManager: key '{}' is node ID, returning as-is", key);
  return key;
}

void UpstreamSocketManager::markSocketDead(const int fd) {
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

  // Check if this is a used connection by looking for node_id in accepted_reverse_connections_
  auto& sockets = accepted_reverse_connections_[node_id];
  if (sockets.empty()) {
    // This is a used connection. Mark the stats and return. The socket will be closed by the
    // owning UpstreamReverseConnectionIOHandle.
    ENVOY_LOG(debug, "UpstreamSocketManager: Marking used socket dead. node: {} cluster: {} FD: {}",
              node_id, cluster_id, fd);
    // Update Envoy's stats system for production multi-tenant tracking
    // This ensures stats are decremented when connections are removed
    if (auto extension = getUpstreamExtension()) {
      extension->updateConnectionStats(node_id, cluster_id, false /* decrement */);
      ENVOY_LOG(debug,
                "UpstreamSocketManager: decremented stats registry for node '{}' cluster '{}'",
                node_id, cluster_id);
    }

    return;
  }

  // This is an idle connection, find and remove it from the pool
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

      // Update Envoy's stats system for production multi-tenant tracking
      // This ensures stats are decremented when connections are removed
      if (auto extension = getUpstreamExtension()) {
        extension->updateConnectionStats(node_id, cluster_id, false /* decrement */);
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

  // Remove empty node entry from accepted_reverse_connections_
  accepted_reverse_connections_.erase(node_id);
}

void UpstreamSocketManager::onPingResponse(Network::IoHandle& io_handle) {
  const int fd = io_handle.fdDoNotUse();

  Buffer::OwnedImpl buffer;
  const auto ping_size =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::PING_MESSAGE
          .size();
  Api::IoCallUint64Result result = io_handle.read(buffer, absl::make_optional(ping_size));
  if (!result.ok()) {
    ENVOY_LOG(debug, "UpstreamSocketManager: Read error on FD: {}: error - {}", fd,
              result.err_->getErrorDetails());
    markSocketDead(fd);
    return;
  }

  // In this case, there is no read error, but the socket has been closed by the remote
  // peer in a graceful manner, unlike a connection refused, or a reset.
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "UpstreamSocketManager: FD: {}: reverse connection closed", fd);
    markSocketDead(fd);
    return;
  }

  if (result.return_value_ < ping_size) {
    ENVOY_LOG(debug, "UpstreamSocketManager: FD: {}: no complete ping data yet", fd);
    return;
  }

  if (!::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::isPingMessage(
          buffer.toString())) {
    ENVOY_LOG(debug, "UpstreamSocketManager: FD: {}: response is not RPING", fd);
    markSocketDead(fd);
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
    auto buffer = ::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
        createPingResponse();

    auto ping_response_timeout = ping_interval_ / 2;
    fd_to_timer_map_[fd]->enableTimer(ping_response_timeout);
    while (buffer->length() > 0) {
      Api::IoCallUint64Result result = itr->get()->ioHandle().write(*buffer);
      ENVOY_LOG(trace,
                "UpstreamSocketManager: node:{} FD:{}: sending ping request. return_value: {}",
                node_id, fd, result.return_value_);
      if (result.return_value_ == 0) {
        ENVOY_LOG(trace, "UpstreamSocketManager: node:{} FD:{}: sending ping rc {}, error - ",
                  node_id, fd, result.return_value_, result.err_->getErrorDetails());
        if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          ENVOY_LOG(error, "UpstreamSocketManager: node:{} FD:{}: failed to send ping", node_id,
                    fd);
          markSocketDead(fd);
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
  ENVOY_LOG(error, "UpstreamSocketManager: Pinging connections");
  for (auto& itr : accepted_reverse_connections_) {
    pingConnections(itr.first);
  }
  ping_timer_->enableTimer(ping_interval_);
}

UpstreamSocketManager::~UpstreamSocketManager() {
  ENVOY_LOG(debug, "UpstreamSocketManager destructor called");

  // Clean up all active file events and timers first
  for (auto& [fd, event] : fd_to_event_map_) {
    ENVOY_LOG(debug, "UpstreamSocketManager: cleaning up file event for FD: {}", fd);
    event.reset(); // This will cancel the file event
  }
  fd_to_event_map_.clear();

  for (auto& [fd, timer] : fd_to_timer_map_) {
    ENVOY_LOG(debug, "UpstreamSocketManager: cleaning up timer for FD: {}", fd);
    timer.reset(); // This will cancel the timer
  }
  fd_to_timer_map_.clear();

  // Now mark all sockets as dead
  std::vector<int> fds_to_cleanup;
  for (const auto& [fd, node_id] : fd_to_node_map_) {
    fds_to_cleanup.push_back(fd);
  }

  for (int fd : fds_to_cleanup) {
    ENVOY_LOG(debug, "UpstreamSocketManager: marking socket dead in destructor for FD: {}", fd);
    markSocketDead(fd); // false = not used, just cleanup
  }

  // Clear the ping timer
  if (ping_timer_) {
    ping_timer_->disableTimer();
    ping_timer_.reset();
  }
}

REGISTER_FACTORY(ReverseTunnelAcceptor, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
