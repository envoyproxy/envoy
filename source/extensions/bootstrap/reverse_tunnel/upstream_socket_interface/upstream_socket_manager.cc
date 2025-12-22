#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include <algorithm>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

std::vector<UpstreamSocketManager*> UpstreamSocketManager::socket_managers_{};
absl::Mutex UpstreamSocketManager::socket_manager_lock{};

// UpstreamSocketManager implementation
UpstreamSocketManager::UpstreamSocketManager(Event::Dispatcher& dispatcher,
                                             ReverseTunnelAcceptorExtension* extension)
    : dispatcher_(dispatcher), random_generator_(std::make_unique<Random::RandomGeneratorImpl>()),
      extension_(extension) {
  ENVOY_LOG(debug, "reverse_tunnel: creating socket manager with stats integration.");
  ping_timer_ = dispatcher_.createTimer([this]() { pingConnections(); });

  // Register this socket manager instance for rebalancing.
  absl::WriterMutexLock lock(UpstreamSocketManager::socket_manager_lock);
  UpstreamSocketManager::socket_managers_.push_back(this);
}

UpstreamSocketManager&
UpstreamSocketManager::pickLeastLoadedSocketManager(const std::string& node_id,
                                                    const std::string& cluster_id) {
  absl::WriterMutexLock wlock(UpstreamSocketManager::socket_manager_lock);

  // Assume that this worker is the best candidate for sending the reverse.
  // connection socket.
  UpstreamSocketManager* target_socket_manager = this;
  const std::string source_worker = this->dispatcher_.name();

  // Contains the value that we assume to be the minimum value so far.
  int min_node_count = target_socket_manager->node_to_conn_count_map_[node_id];

  // Iterate over UpstreamSocketManager instances of all threads to check.
  // if any of them have a lower number of accepted reverse tunnels for.
  // the node 'node_id'.
  for (UpstreamSocketManager* socket_manager : socket_managers_) {
    int node_count = socket_manager->node_to_conn_count_map_[node_id];

    if (node_count < min_node_count) {
      target_socket_manager = socket_manager;
      min_node_count = node_count;
    }
  }

  const std::string dest_worker = target_socket_manager->dispatcher_.name();

  // Increment the reverse connection count of the chosen handler.
  if (source_worker != dest_worker) {
    ENVOY_LOG(info,
              "reverse_tunnel: Rebalancing socket from worker {} to worker {} with min "
              "count {} for node {} cluster {}",
              source_worker, dest_worker, target_socket_manager->node_to_conn_count_map_[node_id],
              node_id, cluster_id);
  }
  target_socket_manager->node_to_conn_count_map_[node_id]++;
  ENVOY_LOG(debug, "reverse_tunnel: Incremented count for node {}: {}", node_id,
            target_socket_manager->node_to_conn_count_map_[node_id]);
  return *target_socket_manager;
}

void UpstreamSocketManager::handoffSocketToWorker(const std::string& node_id,
                                                  const std::string& cluster_id,
                                                  Network::ConnectionSocketPtr socket,
                                                  const std::chrono::seconds& ping_interval) {
  dispatcher_.post(
      [this, node_id, cluster_id, ping_interval, socket = std::move(socket)]() mutable -> void {
        this->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                  true /* rebalanced */);
      });
}

void UpstreamSocketManager::addConnectionSocket(const std::string& node_id,
                                                const std::string& cluster_id,
                                                Network::ConnectionSocketPtr socket,
                                                const std::chrono::seconds& ping_interval,
                                                bool rebalanced) {
  // If not already rebalanced, check if we should move this socket to a different worker thread.
  if (!rebalanced) {
    UpstreamSocketManager& target_manager = pickLeastLoadedSocketManager(node_id, cluster_id);
    if (&target_manager != this) {
      ENVOY_LOG(debug,
                "reverse_tunnel: Rebalancing socket to a different worker thread for node: "
                "{} cluster: {}",
                node_id, cluster_id);
      target_manager.handoffSocketToWorker(node_id, cluster_id, std::move(socket), ping_interval);
      return;
    }
  }

  ENVOY_LOG(debug, "reverse_tunnel: adding connection for node: {}, cluster: {}.", node_id,
            cluster_id);

  // Both node_id and cluster_id are mandatory for consistent state management and stats tracking.
  if (node_id.empty() || cluster_id.empty()) {
    ENVOY_LOG(error,
              "reverse_tunnel: node_id or cluster_id cannot be empty. node: '{}', cluster: '{}'.",
              node_id, cluster_id);
    return;
  }

  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& connectionKey = socket->connectionInfoProvider().localAddress()->asString();

  ENVOY_LOG(debug, "reverse_tunnel: adding socket with FD: {} for node: {}, cluster: {}.", fd,
            node_id, cluster_id);

  // Store node -> cluster mapping.
  ENVOY_LOG(trace, "reverse_tunnel: adding mapping node {} -> cluster {}.", node_id, cluster_id);
  if (node_to_cluster_map_.find(node_id) == node_to_cluster_map_.end()) {
    node_to_cluster_map_[node_id] = cluster_id;
    cluster_to_node_info_map_[cluster_id].nodes.push_back(node_id);
  }

  fd_to_node_map_[fd] = node_id;
  fd_to_cluster_map_[fd] = cluster_id;
  // Initialize the ping timer before adding the socket to accepted_reverse_connections_.
  // This is to prevent a race condition between pingConnections() and addConnectionSocket().
  // where the timer is not initialized when pingConnections() tries to enable it.
  fd_to_timer_map_[fd] = dispatcher_.createTimer([this, fd]() { onPingTimeout(fd); });

  // If local Envoy is responding to reverse connections, add the socket to.
  // accepted_reverse_connections_. Thereafter, initiate ping keepalives on the socket.
  accepted_reverse_connections_[node_id].push_back(std::move(socket));
  Network::ConnectionSocketPtr& socket_ref = accepted_reverse_connections_[node_id].back();

  // Update stats registry.
  if (auto extension = getUpstreamExtension()) {
    extension->updateConnectionStats(node_id, cluster_id, true /* increment */);
    ENVOY_LOG(debug, "reverse_tunnel: updated stats registry for node '{}' cluster '{}'.", node_id,
              cluster_id);
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

  // Initiate ping keepalives on the socket.
  tryEnablePingTimer(std::chrono::seconds(ping_interval.count()));

  ENVOY_LOG(debug, "reverse_tunnel: added socket to maps. node: {} connection key: {} fd: {}.",
            node_id, connectionKey, fd);
}

Network::ConnectionSocketPtr
UpstreamSocketManager::getConnectionSocket(const std::string& node_id) {

  ENVOY_LOG(debug, "reverse_tunnel: getConnectionSocket() called with node_id: {}.", node_id);

  if (node_to_cluster_map_.find(node_id) == node_to_cluster_map_.end()) {
    ENVOY_LOG(error, "reverse_tunnel: cluster to node mapping changed for node: {}.", node_id);
    return nullptr;
  }

  const std::string& cluster_id = node_to_cluster_map_[node_id];

  ENVOY_LOG(debug, "reverse_tunnel: looking for socket. node: {} cluster: {}.", node_id,
            cluster_id);

  // Find first available socket for the node.
  auto node_sockets_it = accepted_reverse_connections_.find(node_id);
  if (node_sockets_it == accepted_reverse_connections_.end() || node_sockets_it->second.empty()) {
    ENVOY_LOG(debug, "reverse_tunnel: no available sockets for node: {}.", node_id);
    return nullptr;
  }

  // Debugging: Print the number of free sockets on this worker thread.
  ENVOY_LOG(trace, "reverse_tunnel: found {} sockets for node: {}.", node_sockets_it->second.size(),
            node_id);

  // Fetch the socket from the accepted_reverse_connections_ and remove it from the list.
  Network::ConnectionSocketPtr socket(std::move(node_sockets_it->second.front()));
  node_sockets_it->second.pop_front();

  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& remoteConnectionKey =
      socket->connectionInfoProvider().remoteAddress()->asString();

  ENVOY_LOG(debug,
            "reverse_tunnel: reverse connection socket found. fd: {} connection key: {} "
            "node: {} cluster: {}.",
            fd, remoteConnectionKey, node_id, cluster_id);

  fd_to_event_map_.erase(fd);
  fd_to_timer_map_.erase(fd);

  return socket;
}

std::string UpstreamSocketManager::getNodeWithSocket(const std::string& key) {
  ENVOY_LOG(trace, "reverse_tunnel: getNodeWithSocket() called with key: {}.", key);

  // Check if key exists as a cluster ID by looking at cluster_to_node_info_map_.
  auto cluster_it = cluster_to_node_info_map_.find(key);
  if (cluster_it != cluster_to_node_info_map_.end() && !cluster_it->second.nodes.empty()) {
    // Key is a cluster ID, use round-robin to select a node.
    auto& cluster_info = cluster_it->second;
    const auto& nodes = cluster_info.nodes;

    // Select node at current index and advance for next call.
    const std::string& selected_node = nodes[cluster_info.round_robin_index % nodes.size()];
    cluster_info.round_robin_index = (cluster_info.round_robin_index + 1) % nodes.size();

    ENVOY_LOG(debug, "reverse_tunnel: key '{}' is a cluster ID; returning node {} via round-robin.",
              key, selected_node);
    return selected_node;
  }

  // Key not found in cluster map, treat it as a node ID and return it directly.
  ENVOY_LOG(trace, "reverse_tunnel: key '{}' treated as node ID; returning as-is.", key);
  return key;
}

bool UpstreamSocketManager::hasAnySocketsForNode(const std::string& node_id) {
  // Check idle sockets first via map lookup O(1) instead of iterating fd_to_node_map_ O(n).
  // Note: fd_to_node_map_ contains all FDs (idle + used), but this is faster for the common case.
  auto idle_it = accepted_reverse_connections_.find(node_id);
  if (idle_it != accepted_reverse_connections_.end() && !idle_it->second.empty()) {
    return true;
  }

  // Check if node has any used sockets.
  for (const auto& [fd, n_id] : fd_to_node_map_) {
    if (n_id == node_id) {
      return true;
    }
  }

  return false;
}

void UpstreamSocketManager::markSocketDead(const int fd) {
  ENVOY_LOG(trace, "reverse_tunnel: markSocketDead called for fd {}.", fd);

  auto node_it = fd_to_node_map_.find(fd);
  if (node_it == fd_to_node_map_.end()) {
    ENVOY_LOG(warn, "reverse_tunnel: fd {} not found in fd_to_node_map_.", fd);
    return;
  }
  const std::string node_id = node_it->second;

  // Get cluster_id from fd_to_cluster_map_. We use the fd_to_cluster_map_ to get the cluster_id
  // and not the cluster_to_node_info_map_ because the node might have changed clusters before the
  // socket is marked dead, but the FD will always be tied to the same cluster in
  // fd_to_cluster_map_.
  std::string cluster_id;
  auto cluster_it = fd_to_cluster_map_.find(fd);
  if (cluster_it == fd_to_cluster_map_.end()) {
    ENVOY_LOG(warn, "reverse_tunnel: fd {} not found in fd_to_cluster_map_.", fd);
    // Try to get cluster_id from node_to_cluster_map_ as fallback.
    auto node_cluster_it = node_to_cluster_map_.find(node_id);
    if (node_cluster_it != node_to_cluster_map_.end()) {
      cluster_id = node_cluster_it->second;
    }
  } else {
    cluster_id = cluster_it->second;
  }
  ENVOY_LOG(debug, "reverse_tunnel: found node '{}' cluster '{}' for fd: {}", node_id, cluster_id,
            fd);

  // Remove FD from tracking maps before checking remaining sockets.
  fd_to_node_map_.erase(fd);
  fd_to_cluster_map_.erase(fd);

  // Determine if this is an idle or used socket by searching for the FD in the idle pool.
  auto& sockets = accepted_reverse_connections_[node_id];
  bool is_idle_socket = false;

  for (auto itr = sockets.begin(); itr != sockets.end(); itr++) {
    if (fd == itr->get()->ioHandle().fdDoNotUse()) {
      // Found the FD in idle pool, this is an idle socket.
      ENVOY_LOG(debug, "reverse_tunnel: marking idle socket dead. node: {} cluster: {} fd: {}.",
                node_id, cluster_id, fd);
      ::shutdown(fd, SHUT_RDWR);
      itr = sockets.erase(itr);
      is_idle_socket = true;

      fd_to_event_map_.erase(fd);
      fd_to_timer_map_.erase(fd);
      break;
    }
  }

  if (!is_idle_socket) {
    // FD not found in idle pool, this is a used socket.
    // The socket will be closed by the owning UpstreamReverseConnectionIOHandle.
    ENVOY_LOG(debug, "reverse_tunnel: marking used socket dead. node: {} cluster: {} fd: {}.",
              node_id, cluster_id, fd);
  }

  // Update Envoy's stats system.
  if (auto extension = getUpstreamExtension()) {
    extension->updateConnectionStats(node_id, cluster_id, false /* decrement */);
    ENVOY_LOG(trace, "reverse_tunnel: decremented stats registry for node '{}' cluster '{}'.",
              node_id, cluster_id);
  }

  // Only clean up node-to-cluster mappings if this node has no remaining sockets (idle or used).
  if (!hasAnySocketsForNode(node_id)) {
    ENVOY_LOG(debug,
              "reverse_tunnel: node '{}' has no remaining sockets, cleaning up cluster mappings.",
              node_id);
    cleanStaleNodeEntry(node_id);
  } else {
    ENVOY_LOG(trace, "reverse_tunnel: node '{}' still has remaining sockets, keeping in maps.",
              node_id);
  }
}

void UpstreamSocketManager::tryEnablePingTimer(const std::chrono::seconds& ping_interval) {
  ENVOY_LOG(debug, "reverse_tunnel: trying to enable ping timer, ping interval: {}",
            ping_interval.count());
  if (ping_interval_ != std::chrono::seconds::zero()) {
    return;
  }
  ENVOY_LOG(debug, "reverse_tunnel: enabling ping timer, ping interval: {}", ping_interval.count());
  ping_interval_ = ping_interval;
  ping_timer_->enableTimer(ping_interval_);
}

void UpstreamSocketManager::cleanStaleNodeEntry(const std::string& node_id) {
  // Clean the given node ID if there are no active sockets.
  if (accepted_reverse_connections_.find(node_id) != accepted_reverse_connections_.end() &&
      accepted_reverse_connections_[node_id].size() > 0) {
    ENVOY_LOG(trace, "reverse_tunnel: found {} active sockets for node {}.",
              accepted_reverse_connections_[node_id].size(), node_id);
    return;
  }
  ENVOY_LOG(debug, "reverse_tunnel: cleaning stale node entry for node {}.", node_id);

  // Check if given node-id is present in node_to_cluster_map_. If present,
  // fetch the corresponding cluster-id and remove the node from the cluster's node list.
  const auto& node_itr = node_to_cluster_map_.find(node_id);
  if (node_itr != node_to_cluster_map_.end()) {
    const auto& cluster_itr = cluster_to_node_info_map_.find(node_itr->second);
    if (cluster_itr != cluster_to_node_info_map_.end()) {
      auto& nodes = cluster_itr->second.nodes;
      const auto& node_entry_itr = find(nodes.begin(), nodes.end(), node_id);

      if (node_entry_itr != nodes.end()) {
        ENVOY_LOG(trace, "reverse_tunnel: removing stale node {} from cluster {}.", node_id,
                  cluster_itr->first);
        nodes.erase(node_entry_itr);

        // If the cluster has no more nodes, remove the entire cluster entry.
        if (nodes.empty()) {
          ENVOY_LOG(trace, "reverse_tunnel: removing empty cluster {}.", cluster_itr->first);
          cluster_to_node_info_map_.erase(cluster_itr);
        }
      }
    }
    node_to_cluster_map_.erase(node_itr);
  }

  // Remove empty node entry from accepted_reverse_connections_.
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
    ENVOY_LOG(debug, "reverse_tunnel: Read error on FD: {}: error - {}", fd,
              result.err_->getErrorDetails());
    markSocketDead(fd);
    return;
  }

  // In this case, there is no read error, but the socket has been closed by the remote.
  // peer in a graceful manner, unlike a connection refused, or a reset.
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "reverse_tunnel: FD: {}: reverse connection closed", fd);
    markSocketDead(fd);
    return;
  }

  if (result.return_value_ < ping_size) {
    ENVOY_LOG(debug, "reverse_tunnel: FD: {}: no complete ping data yet", fd);
    return;
  }

  const char* data = static_cast<const char*>(buffer.linearize(ping_size));
  absl::string_view view{data, static_cast<size_t>(ping_size)};
  if (!::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::isPingMessage(
          view)) {
    ENVOY_LOG(debug, "reverse_tunnel: response is not RPING. fd: {}.", fd);
    // Treat as a miss; do not immediately kill unless threshold crossed.
    onPingTimeout(fd);
    return;
  }
  ENVOY_LOG(trace, "reverse_tunnel: received ping response. fd: {}.", fd);
  fd_to_timer_map_[fd]->disableTimer();
  // Reset miss counter on success.
  fd_to_miss_count_.erase(fd);
}

void UpstreamSocketManager::pingConnections(const std::string& node_id) {
  ENVOY_LOG(trace, "reverse_tunnel: pinging connections for node {}.", node_id);
  auto& sockets = accepted_reverse_connections_[node_id];
  ENVOY_LOG(trace, "reverse_tunnel: number of sockets for node {} is {}.", node_id, sockets.size());

  auto itr = sockets.begin();
  while (itr != sockets.end()) {
    int fd = itr->get()->ioHandle().fdDoNotUse();
    auto buffer = ::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
        createPingResponse();

    auto ping_response_timeout = ping_interval_ / 2;
    fd_to_timer_map_[fd]->enableTimer(ping_response_timeout);

    // Use a flag to signal whether the socket needs to be marked dead. If the socket is marked
    // dead. in markSocketDead(), it is erased from the list, and the iterator becomes invalid. We
    // need to. break out of the loop to avoid a use after free error.
    bool socket_dead = false;
    while (buffer->length() > 0) {
      Api::IoCallUint64Result result = itr->get()->ioHandle().write(*buffer);
      ENVOY_LOG(trace, "reverse_tunnel: node:{} FD:{}: sending ping request. return_value: {}",
                node_id, fd, result.return_value_);
      if (result.return_value_ == 0) {
        ENVOY_LOG(trace, "reverse_tunnel: node:{} FD:{}: sending ping rc {}, error - ", node_id, fd,
                  result.return_value_, result.err_->getErrorDetails());
        if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          ENVOY_LOG(error, "reverse_tunnel: node:{} FD:{}: failed to send ping", node_id, fd);
          markSocketDead(fd);
          socket_dead = true;
          break;
        }
      }
    }

    if (buffer->length() > 0) {
      // Move to next socket if current one could not be fully written.
      ++itr;
      continue;
    }

    if (socket_dead) {
      // Socket was marked dead; the iterator is now invalid. Break out of the loop.
      break;
    }

    // Move to next socket.
    ++itr;
  }
}

void UpstreamSocketManager::pingConnections() {
  ENVOY_LOG(trace, "reverse_tunnel: pinging connections.");
  // If the last socket for a node errors out the map entry is cleared. So we cant use normal for
  // loops.
  for (auto itr = accepted_reverse_connections_.begin();
       itr != accepted_reverse_connections_.end();) {
    auto next = std::next(itr);

    pingConnections(itr->first);
    itr = next;
  }
  ping_timer_->enableTimer(ping_interval_);
}

void UpstreamSocketManager::onPingTimeout(const int fd) {
  ENVOY_LOG(debug, "reverse_tunnel: ping timeout or invalid ping. fd: {}.", fd);
  // Increment miss count and evaluate threshold.
  const uint32_t misses = ++fd_to_miss_count_[fd];
  ENVOY_LOG(trace, "reverse_tunnel: miss count {}. fd: {}.", misses, fd);
  if (misses >= miss_threshold_) {
    ENVOY_LOG(debug, "reverse_tunnel: fd {} exceeded miss threshold {}; marking dead.", fd,
              miss_threshold_);
    fd_to_miss_count_.erase(fd);
    markSocketDead(fd);
  }
}

UpstreamSocketManager::~UpstreamSocketManager() {
  ENVOY_LOG(debug, "reverse_tunnel: destructor called.");

  // Clean up all active file events and timers first.
  for (auto& [fd, event] : fd_to_event_map_) {
    ENVOY_LOG(trace, "reverse_tunnel: cleaning up file event. fd: {}.", fd);
    event.reset(); // This will cancel the file event.
  }
  fd_to_event_map_.clear();

  for (auto& [fd, timer] : fd_to_timer_map_) {
    ENVOY_LOG(trace, "reverse_tunnel: cleaning up timer. fd: {}.", fd);
    timer.reset(); // This will cancel the timer.
  }
  fd_to_timer_map_.clear();

  // Now mark all sockets as dead.
  std::vector<int> fds_to_cleanup;
  for (const auto& [fd, node_id] : fd_to_node_map_) {
    fds_to_cleanup.push_back(fd);
  }

  for (int fd : fds_to_cleanup) {
    ENVOY_LOG(trace, "reverse_tunnel: marking socket dead in destructor. fd: {}.", fd);
    markSocketDead(fd); // false = not used, just cleanup.
  }

  // Clear any remaining fd mappings.
  fd_to_node_map_.clear();
  fd_to_cluster_map_.clear();

  // Clear the ping timer.
  if (ping_timer_) {
    ping_timer_->disableTimer();
    ping_timer_.reset();
  }

  // Remove this instance from the global socket managers list.
  absl::WriterMutexLock lock(UpstreamSocketManager::socket_manager_lock);
  auto it = std::find(socket_managers_.begin(), socket_managers_.end(), this);
  if (it != socket_managers_.end()) {
    socket_managers_.erase(it);
  }
}

} // namespace ReverseConnection.
} // namespace Bootstrap.
} // namespace Extensions.
} // namespace Envoy.
