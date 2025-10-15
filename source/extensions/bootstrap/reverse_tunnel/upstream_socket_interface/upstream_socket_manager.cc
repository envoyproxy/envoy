#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

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

// UpstreamSocketManager implementation
UpstreamSocketManager::UpstreamSocketManager(Event::Dispatcher& dispatcher,
                                             ReverseTunnelAcceptorExtension* extension)
    : dispatcher_(dispatcher), random_generator_(std::make_unique<Random::RandomGeneratorImpl>()),
      extension_(extension) {
  ENVOY_LOG(debug, "reverse_tunnel: creating socket manager with stats integration.");
  ping_timer_ = dispatcher_.createTimer([this]() { pingConnections(); });
}

void UpstreamSocketManager::addConnectionSocket(const std::string& node_id,
                                                const std::string& cluster_id,
                                                Network::ConnectionSocketPtr socket,
                                                const std::chrono::seconds& ping_interval, bool) {
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
    cluster_to_node_map_[cluster_id].push_back(node_id);
  }

  fd_to_node_map_[fd] = node_id;
  // Initialize the ping timer before adding the socket to accepted_reverse_connections_.
  // This is to prevent a race condition between pingConnections() and addConnectionSocket()
  // where the timer is not initialized when pingConnections() tries to enable it.
  fd_to_timer_map_[fd] = dispatcher_.createTimer([this, fd]() { onPingTimeout(fd); });

  // If local Envoy is responding to reverse connections, add the socket to
  // accepted_reverse_connections_. Thereafter, initiate ping keepalives on the socket.
  accepted_reverse_connections_[node_id].push_back(std::move(socket));
  Network::ConnectionSocketPtr& socket_ref = accepted_reverse_connections_[node_id].back();

  // Update stats registry.
  if (auto extension = getUpstreamExtension()) {
    extension->updateConnectionStats(node_id, cluster_id, true /* increment */);
    ENVOY_LOG(debug, "UpstreamSocketManager: updated stats registry for node '{}' cluster '{}'.",
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

  // Initiate ping keepalives on the socket.
  tryEnablePingTimer(std::chrono::seconds(ping_interval.count()));

  ENVOY_LOG(debug,
            "UpstreamSocketManager: added socket to maps. node: {} connection key: {} fd: {}.",
            node_id, connectionKey, fd);
}

Network::ConnectionSocketPtr
UpstreamSocketManager::getConnectionSocket(const std::string& node_id) {

  ENVOY_LOG(debug, "UpstreamSocketManager: getConnectionSocket() called with node_id: {}.",
            node_id);

  if (node_to_cluster_map_.find(node_id) == node_to_cluster_map_.end()) {
    ENVOY_LOG(error, "UpstreamSocketManager: cluster to node mapping changed for node: {}.",
              node_id);
    return nullptr;
  }

  const std::string& cluster_id = node_to_cluster_map_[node_id];

  ENVOY_LOG(debug, "UpstreamSocketManager: looking for socket. node: {} cluster: {}.", node_id,
            cluster_id);

  // Find first available socket for the node.
  auto node_sockets_it = accepted_reverse_connections_.find(node_id);
  if (node_sockets_it == accepted_reverse_connections_.end() || node_sockets_it->second.empty()) {
    ENVOY_LOG(debug, "UpstreamSocketManager: no available sockets for node: {}.", node_id);
    return nullptr;
  }

  // Debugging: Print the number of free sockets on this worker thread.
  ENVOY_LOG(trace, "UpstreamSocketManager: found {} sockets for node: {}.",
            node_sockets_it->second.size(), node_id);

  // Fetch the socket from the accepted_reverse_connections_ and remove it from the list.
  Network::ConnectionSocketPtr socket(std::move(node_sockets_it->second.front()));
  node_sockets_it->second.pop_front();

  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& remoteConnectionKey =
      socket->connectionInfoProvider().remoteAddress()->asString();

  ENVOY_LOG(debug,
            "UpstreamSocketManager: reverse connection socket found. fd: {} connection key: {} "
            "node: {} cluster: {}.",
            fd, remoteConnectionKey, node_id, cluster_id);

  fd_to_event_map_.erase(fd);
  fd_to_timer_map_.erase(fd);

  cleanStaleNodeEntry(node_id);

  return socket;
}

std::string UpstreamSocketManager::getNodeID(const std::string& key) {
  ENVOY_LOG(trace, "UpstreamSocketManager: getNodeID() called with key: {}.", key);

  // Check if key exists as a cluster ID by looking at cluster_to_node_map_.
  auto cluster_nodes_it = cluster_to_node_map_.find(key);
  if (cluster_nodes_it != cluster_to_node_map_.end()) {
    // Key is a cluster ID, find a node in this cluster with idle connections.
    for (const std::string& node_id : cluster_nodes_it->second) {
      auto node_sockets_it = accepted_reverse_connections_.find(node_id);
      if (node_sockets_it != accepted_reverse_connections_.end() &&
          !node_sockets_it->second.empty()) {
        // Found a node in this cluster with idle connections.
        ENVOY_LOG(debug,
                  "UpstreamSocketManager: key '{}' is a cluster ID; returning node {} "
                  "with {} idle connections.",
                  key, node_id, node_sockets_it->second.size());
        return node_id;
      }
    }
    ENVOY_LOG(debug,
              "UpstreamSocketManager: key '{}' is a cluster ID but no nodes have "
              "idle connections; returning as-is.",
              key);
  }

  // Treat it as a node ID and return it directly.
  ENVOY_LOG(trace, "UpstreamSocketManager: key '{}' is a node ID; returning as-is.", key);
  return key;
}

void UpstreamSocketManager::markSocketDead(const int fd) {
  ENVOY_LOG(trace, "UpstreamSocketManager: markSocketDead called for fd {}.", fd);

  auto node_it = fd_to_node_map_.find(fd);
  if (node_it == fd_to_node_map_.end()) {
    ENVOY_LOG(debug, "UpstreamSocketManager: fd {} not found in fd_to_node_map_.", fd);
    return;
  }

  const std::string node_id = node_it->second; // Make a COPY, not a reference
  ENVOY_LOG(trace, "UpstreamSocketManager: found node '{}' for fd {}.", node_id, fd);

  std::string cluster_id = (node_to_cluster_map_.find(node_id) != node_to_cluster_map_.end())
                               ? node_to_cluster_map_[node_id]
                               : "";
  fd_to_node_map_.erase(fd); // Now it's safe to erase since node_id is a copy

  // Check if this is a used connection by looking for node_id in accepted_reverse_connections_
  auto& sockets = accepted_reverse_connections_[node_id];
  if (sockets.empty()) {
    // This is a used connection. Mark the stats and return. The socket will be closed by the
    // owning UpstreamReverseConnectionIOHandle.
    ENVOY_LOG(debug,
              "UpstreamSocketManager: marking used socket dead. node: {} cluster: {} fd: {}.",
              node_id, cluster_id, fd);
    // Update Envoy's stats system for production multi-tenant tracking
    // This ensures stats are decremented when connections are removed
    if (auto extension = getUpstreamExtension()) {
      extension->updateConnectionStats(node_id, cluster_id, false /* decrement */);
      ENVOY_LOG(trace,
                "UpstreamSocketManager: decremented stats registry for node '{}' cluster '{}'.",
                node_id, cluster_id);
    }

    return;
  }

  // This is an idle connection, find and remove it from the pool.
  bool socket_found = false;
  for (auto itr = sockets.begin(); itr != sockets.end(); itr++) {
    if (fd == itr->get()->ioHandle().fdDoNotUse()) {
      ENVOY_LOG(debug, "UpstreamSocketManager: marking socket dead. node: {} cluster: {} fd: {}.",
                node_id, cluster_id, fd);
      ::shutdown(fd, SHUT_RDWR);
      itr = sockets.erase(itr);
      socket_found = true;

      fd_to_event_map_.erase(fd);
      fd_to_timer_map_.erase(fd);

      // Update Envoy's stats system for production multi-tenant tracking.
      // This ensures stats are decremented when connections are removed.
      if (auto extension = getUpstreamExtension()) {
        extension->updateConnectionStats(node_id, cluster_id, false /* decrement */);
        ENVOY_LOG(trace,
                  "UpstreamSocketManager: decremented stats registry for node '{}' cluster '{}'.",
                  node_id, cluster_id);
      }
      break;
    }
  }

  if (!socket_found) {
    ENVOY_LOG(
        error,
        "UpstreamSocketManager: attempted to mark a non-existent socket dead. node: {} fd: {}.",
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
  // Clean the given node ID if there are no active sockets.
  if (accepted_reverse_connections_.find(node_id) != accepted_reverse_connections_.end() &&
      accepted_reverse_connections_[node_id].size() > 0) {
    ENVOY_LOG(trace, "UpstreamSocketManager: found {} active sockets for node {}.",
              accepted_reverse_connections_[node_id].size(), node_id);
    return;
  }
  ENVOY_LOG(debug, "UpstreamSocketManager: cleaning stale node entry for node {}.", node_id);

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
        ENVOY_LOG(trace, "UpstreamSocketManager: removing stale node {} from cluster {}.", node_id,
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

  const char* data = static_cast<const char*>(buffer.linearize(ping_size));
  absl::string_view view{data, static_cast<size_t>(ping_size)};
  if (!::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::isPingMessage(
          view)) {
    ENVOY_LOG(debug, "UpstreamSocketManager: response is not RPING. fd: {}.", fd);
    // Treat as a miss; do not immediately kill unless threshold crossed.
    onPingTimeout(fd);
    return;
  }
  ENVOY_LOG(trace, "UpstreamSocketManager: received ping response. fd: {}.", fd);
  fd_to_timer_map_[fd]->disableTimer();
  // Reset miss counter on success.
  fd_to_miss_count_.erase(fd);
}

void UpstreamSocketManager::pingConnections(const std::string& node_id) {
  ENVOY_LOG(trace, "UpstreamSocketManager: pinging connections for node {}.", node_id);
  auto& sockets = accepted_reverse_connections_[node_id];
  ENVOY_LOG(trace, "UpstreamSocketManager: number of sockets for node {} is {}.", node_id,
            sockets.size());

  auto itr = sockets.begin();
  while (itr != sockets.end()) {
    int fd = itr->get()->ioHandle().fdDoNotUse();
    auto buffer = ::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
        createPingResponse();

    auto ping_response_timeout = ping_interval_ / 2;
    fd_to_timer_map_[fd]->enableTimer(ping_response_timeout);

    // Use a flag to signal whether the socket needs to be marked dead. If the socket is marked dead
    // in markSocketDead(), it is erased from the list, and the iterator becomes invalid. We need to
    // break out of the loop to avoid a use after free error.
    bool socket_dead = false;
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

    // Move to next socket
    ++itr;
  }
}

void UpstreamSocketManager::pingConnections() {
  ENVOY_LOG(trace, "UpstreamSocketManager: pinging connections.");
  for (auto& itr : accepted_reverse_connections_) {
    pingConnections(itr.first);
  }
  ping_timer_->enableTimer(ping_interval_);
}

void UpstreamSocketManager::onPingTimeout(const int fd) {
  ENVOY_LOG(debug, "UpstreamSocketManager: ping timeout or invalid ping. fd: {}.", fd);
  // Increment miss count and evaluate threshold.
  const uint32_t misses = ++fd_to_miss_count_[fd];
  ENVOY_LOG(trace, "UpstreamSocketManager: miss count {}. fd: {}.", misses, fd);
  if (misses >= miss_threshold_) {
    ENVOY_LOG(debug, "UpstreamSocketManager: fd {} exceeded miss threshold {}; marking dead.", fd,
              miss_threshold_);
    fd_to_miss_count_.erase(fd);
    markSocketDead(fd);
  }
}

UpstreamSocketManager::~UpstreamSocketManager() {
  ENVOY_LOG(debug, "UpstreamSocketManager: destructor called.");

  // Clean up all active file events and timers first
  for (auto& [fd, event] : fd_to_event_map_) {
    ENVOY_LOG(trace, "UpstreamSocketManager: cleaning up file event. fd: {}.", fd);
    event.reset(); // This will cancel the file event.
  }
  fd_to_event_map_.clear();

  for (auto& [fd, timer] : fd_to_timer_map_) {
    ENVOY_LOG(trace, "UpstreamSocketManager: cleaning up timer. fd: {}.", fd);
    timer.reset(); // This will cancel the timer.
  }
  fd_to_timer_map_.clear();

  // Now mark all sockets as dead
  std::vector<int> fds_to_cleanup;
  for (const auto& [fd, node_id] : fd_to_node_map_) {
    fds_to_cleanup.push_back(fd);
  }

  for (int fd : fds_to_cleanup) {
    ENVOY_LOG(trace, "UpstreamSocketManager: marking socket dead in destructor. fd: {}.", fd);
    markSocketDead(fd); // false = not used, just cleanup
  }

  // Clear the ping timer
  if (ping_timer_) {
    ping_timer_->disableTimer();
    ping_timer_.reset();
  }
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
