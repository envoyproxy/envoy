#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/event/real_time_system.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/ssl_handshaker.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/rc_connection_wrapper.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_address.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// ReverseConnectionIOHandle implementation
ReverseConnectionIOHandle::ReverseConnectionIOHandle(os_fd_t fd,
                                                     const ReverseConnectionSocketConfig& config,
                                                     Upstream::ClusterManager& cluster_manager,
                                                     ReverseTunnelInitiatorExtension* extension,
                                                     Stats::Scope&)
    : IoSocketHandleImpl(fd), config_(config), cluster_manager_(cluster_manager),
      extension_(extension), original_socket_fd_(fd) {
  ENVOY_LOG_MISC(debug,
                 "Created reverse_tunnel: fd={}, src_node={}, src_cluster: {}, num_clusters={}",
                 fd_, config_.src_node_id, config_.src_cluster_id, config_.remote_clusters.size());
}

ReverseConnectionIOHandle::~ReverseConnectionIOHandle() {
  ENVOY_LOG_MISC(debug, "Destroying ReverseConnectionIOHandle - performing cleanup.");
  cleanup();
}

void ReverseConnectionIOHandle::cleanup() {
  ENVOY_LOG_MISC(debug, "Starting cleanup of reverse connection resources.");

  // Clean up pipe trigger mechanism first to prevent use-after-free.
  ENVOY_LOG_MISC(trace,
                 "reverse_tunnel: cleaning up trigger pipe; "
                 "trigger_pipe_write_fd_={}, trigger_pipe_read_fd_={}",
                 trigger_pipe_write_fd_, trigger_pipe_read_fd_);
  if (trigger_pipe_write_fd_ >= 0) {
    ::close(trigger_pipe_write_fd_);
    trigger_pipe_write_fd_ = -1;
  }
  if (trigger_pipe_read_fd_ >= 0) {
    ::close(trigger_pipe_read_fd_);
    trigger_pipe_read_fd_ = -1;
  }

  // Cancel the retry timer safely.
  if (rev_conn_retry_timer_ && rev_conn_retry_timer_->enabled()) {
    ENVOY_LOG_MISC(trace, "reverse_tunnel: cancelling and resetting retry timer.");
    rev_conn_retry_timer_.reset();
  }

  // Graceful shutdown of connection wrappers with exception safety.
  ENVOY_LOG_MISC(debug, "Gracefully shutting down {} connection wrappers.",
                 connection_wrappers_.size());

  // Move wrappers for deferred cleanup.
  std::vector<std::unique_ptr<RCConnectionWrapper>> wrappers_to_delete;
  for (auto& wrapper : connection_wrappers_) {
    if (wrapper) {
      ENVOY_LOG(debug, "Moving connection wrapper for deferred cleanup.");
      wrappers_to_delete.push_back(std::move(wrapper));
    }
  }

  // Clear containers safely.
  connection_wrappers_.clear();
  conn_wrapper_to_host_map_.clear();

  // Clean up wrappers with safe deletion.
  for (auto& wrapper : wrappers_to_delete) {
    if (wrapper && isThreadLocalDispatcherAvailable()) {
      getThreadLocalDispatcher().deferredDelete(std::move(wrapper));
    } else {
      // Direct cleanup when dispatcher not available.
      wrapper.reset();
    }
  }

  // Clear cluster to hosts mapping.
  cluster_to_resolved_hosts_map_.clear();
  host_to_conn_info_map_.clear();

  // Clear established connections queue safely.
  size_t queue_size = established_connections_.size();
  ENVOY_LOG(debug, "reverse_tunnel: Cleaning up {} established connections.", queue_size);

  while (!established_connections_.empty()) {
    auto connection = std::move(established_connections_.front());
    established_connections_.pop();

    if (connection) {
      auto state = connection->state();
      if (state == Envoy::Network::Connection::State::Open) {
        connection->close(Envoy::Network::ConnectionCloseType::FlushWrite);
        ENVOY_LOG(debug, "Closed established connection.");
      } else {
        ENVOY_LOG(debug, "Connection already in state: {}.", static_cast<int>(state));
      }
    }
  }
  ENVOY_LOG(debug, "reverse_tunnel: Completed established connections cleanup.");

  ENVOY_LOG(debug, "reverse_tunnel: Completed cleanup of reverse connection resources.");
}

Api::SysCallIntResult ReverseConnectionIOHandle::listen(int) {
  // No-op for reverse connections.
  return Api::SysCallIntResult{0, 0};
}

void ReverseConnectionIOHandle::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                    Event::FileReadyCb cb,
                                                    Event::FileTriggerType trigger,
                                                    uint32_t events) {
  // Reverse connections should be initiated when initializeFileEvent() is called on a worker
  // thread.
  ENVOY_LOG(debug,
            "ReverseConnectionIOHandle::initializeFileEvent() called on thread: {} for fd={}",
            dispatcher.name(), fd_);

  if (is_reverse_conn_started_) {
    ENVOY_LOG(debug, "reverse_tunnel: Skipping initializeFileEvent() call because "
                     "reverse connections are already started");
    return;
  }

  ENVOY_LOG(info, "reverse_tunnel: Starting reverse connections on worker thread '{}'",
            dispatcher.name());

  // Store worker dispatcher
  worker_dispatcher_ = &dispatcher;

  // Create trigger pipe on worker thread.
  if (!isTriggerPipeReady()) {
    createTriggerPipe();
    if (!isTriggerPipeReady()) {
      ENVOY_LOG(error, "Failed to create trigger pipe on worker thread");
      return;
    }
  }

  // Replace the monitored FD with pipe read FD
  // This must happen before any event registration.
  int trigger_fd = getPipeMonitorFd();
  if (trigger_fd != -1) {
    ENVOY_LOG(info, "Replacing monitored FD from {} to pipe read FD {}", fd_, trigger_fd);
    fd_ = trigger_fd;
  }

  // Initialize reverse connections on worker thread.
  if (!rev_conn_retry_timer_) {
    rev_conn_retry_timer_ = dispatcher.createTimer([this]() {
      ENVOY_LOG(debug, "Reverse connection timer triggered on worker thread");
      maintainReverseConnections();
    });
    maintainReverseConnections();
  }

  is_reverse_conn_started_ = true;
  ENVOY_LOG(info, "reverse_tunnel: Reverse connections started on thread '{}'", dispatcher.name());

  // Call parent implementation.
  IoSocketHandleImpl::initializeFileEvent(dispatcher, cb, trigger, events);
}

Envoy::Network::IoHandlePtr ReverseConnectionIOHandle::accept(struct sockaddr* addr,
                                                              socklen_t* addrlen) {
  ENVOY_LOG(debug, "reverse_tunnel: accept() called");
  if (isTriggerPipeReady()) {
    char trigger_byte;
    ssize_t bytes_read = ::read(trigger_pipe_read_fd_, &trigger_byte, 1);
    if (bytes_read == 1) {
      ENVOY_LOG(debug, "reverse_tunnel: received trigger, processing connection.");
      // When a connection is established, a byte is written to the trigger_pipe_write_fd_ and the
      // connection is inserted into the established_connections_ queue. The last connection in the
      // queue is therefore the one that got established last.
      if (!established_connections_.empty()) {
        ENVOY_LOG(debug, "reverse_tunnel: getting connection from queue.");
        auto connection = std::move(established_connections_.front());
        established_connections_.pop();
        // Fill in address information for the reverse tunnel "client".
        // Use actual client address from established connection.
        if (addr && addrlen) {
          const auto& remote_addr = connection->connectionInfoProvider().remoteAddress();

          if (remote_addr) {
            ENVOY_LOG(debug, "reverse_tunnel: using actual client address: {}",
                      remote_addr->asString());
            const sockaddr* sock_addr = remote_addr->sockAddr();
            socklen_t addr_len = remote_addr->sockAddrLen();

            if (*addrlen >= addr_len) {
              memcpy(addr, sock_addr, addr_len); // NOLINT(safe-memcpy)
              *addrlen = addr_len;
              ENVOY_LOG(trace, "reverse_tunnel: copied {} bytes of address data", addr_len);
            } else {
              ENVOY_LOG(warn,
                        "ReverseConnectionIOHandle::accept() - buffer too small for address: "
                        "need {} bytes, have {}",
                        addr_len, *addrlen);
              *addrlen = addr_len; // Still set the required length
            }
          } else {
            ENVOY_LOG(warn, "reverse_tunnel: no remote address available, "
                            "using synthetic localhost address");
            // Fallback to synthetic address only when remote address is unavailable.
            auto synthetic_addr =
                std::make_shared<Envoy::Network::Address::Ipv4Instance>("127.0.0.1", 0);
            const sockaddr* sock_addr = synthetic_addr->sockAddr();
            socklen_t addr_len = synthetic_addr->sockAddrLen();
            if (*addrlen >= addr_len) {
              memcpy(addr, sock_addr, addr_len); // NOLINT(safe-memcpy)
              *addrlen = addr_len;
            } else {
              ENVOY_LOG(error, "reverse_tunnel: buffer too small for synthetic address");
              *addrlen = addr_len;
            }
          }
        }

        const std::string connection_key =
            connection->connectionInfoProvider().localAddress()->asString();
        ENVOY_LOG(debug, "reverse_tunnel: got connection key: {}", connection_key);

        // Instead of moving the socket, duplicate the file descriptor.
        const Network::ConnectionSocketPtr& original_socket = connection->getSocket();
        if (!original_socket || !original_socket->isOpen()) {
          ENVOY_LOG(error, "Original socket is not available or not open");
          return nullptr;
        }

        // Duplicate the file descriptor.
        Network::IoHandlePtr duplicated_handle = original_socket->ioHandle().duplicate();
        if (!duplicated_handle || !duplicated_handle->isOpen()) {
          ENVOY_LOG(error, "Failed to duplicate file descriptor");
          return nullptr;
        }

        os_fd_t original_fd = original_socket->ioHandle().fdDoNotUse();
        os_fd_t duplicated_fd = duplicated_handle->fdDoNotUse();
        ENVOY_LOG(debug, "reverse_tunnel: duplicated fd: original_fd={}, duplicated_fd={}",
                  original_fd, duplicated_fd);

        // Create a new socket with the duplicated handle.
        Network::ConnectionSocketPtr duplicated_socket =
            std::make_unique<Network::ConnectionSocketImpl>(
                std::move(duplicated_handle),
                original_socket->connectionInfoProvider().localAddress(),
                original_socket->connectionInfoProvider().remoteAddress());

        // Reset file events on the duplicated socket to clear any inherited events.
        duplicated_socket->ioHandle().resetFileEvents();

        // Create RAII-based IoHandle with duplicated socket, passing parent pointer and connection
        // key.
        auto io_handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
            std::move(duplicated_socket), this, connection_key);

        ENVOY_LOG(debug, "reverse_tunnel: RAII IoHandle created with duplicated socket "
                         "and protection enabled.");

        // Reset file events on the original socket to prevent any pending operations. The socket
        // fd has been duplicated, so we have an independent fd. Closing the original connection
        // will only close its fd, not affect our duplicated fd.
        //
        // Note: For raw TCP connections, no shutdown() is called during close, only close() on
        // the fd, which doesn't affect the duplicated fd.
        original_socket->ioHandle().resetFileEvents();

        // Close the original connection.
        connection->close(Network::ConnectionCloseType::NoFlush);

        return io_handle;
      }
    } else if (bytes_read == 0) {
      ENVOY_LOG(debug, "reverse_tunnel: trigger pipe closed.");
      return nullptr;
    } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
      ENVOY_LOG(error, "reverse_tunnel: error reading from trigger pipe: {}", errorDetails(errno));
      return nullptr;
    }
  }
  return nullptr;
}

Api::IoCallUint64Result ReverseConnectionIOHandle::read(Buffer::Instance& buffer,
                                                        absl::optional<uint64_t> max_length) {
  ENVOY_LOG(trace, "Read operation - max_length: {}", max_length.value_or(0));
  auto result = IoSocketHandleImpl::read(buffer, max_length);
  return result;
}

Api::IoCallUint64Result ReverseConnectionIOHandle::write(Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "Write operation - {} bytes", buffer.length());
  auto result = IoSocketHandleImpl::write(buffer);
  return result;
}

Api::SysCallIntResult
ReverseConnectionIOHandle::connect(Envoy::Network::Address::InstanceConstSharedPtr address) {
  // This is not used for reverse connections.
  ENVOY_LOG(trace, "Connect operation - address: {}", address->asString());
  // For reverse connections, connect calls are handled through the tunnel mechanism.
  return IoSocketHandleImpl::connect(address);
}

// Note: This close method is called when the ReverseConnectionIOHandle itself is closed, which
// should typically happen when the listener is being drained.
// Individual reverse connections initiated by this ReverseConnectionIOHandle are managed via
// DownstreamReverseConnectionIOHandle RAII ownership.
Api::IoCallUint64Result ReverseConnectionIOHandle::close() {
  ENVOY_LOG(error, "reverse_tunnel: performing graceful shutdown.");

  // Clean up original socket FD
  if (original_socket_fd_ != -1) {
    ENVOY_LOG(error, "Closing original socket FD: {}.", original_socket_fd_);
    ::close(original_socket_fd_);
    original_socket_fd_ = -1;
  }

  // CRITICAL: If we're using pipe trigger FD, let the IoSocketHandleImpl::close()
  // close it and cleanup() set the pipe FDs to -1.
  if (isTriggerPipeReady() && getPipeMonitorFd() == fd_) {
    ENVOY_LOG(error,
              "Skipping close of pipe trigger FD {} - will be handled by base close() method.",
              fd_);
  }

  if (rev_conn_retry_timer_) {
    rev_conn_retry_timer_.reset();
  }

  return IoSocketHandleImpl::close();
}

void ReverseConnectionIOHandle::onEvent(Network::ConnectionEvent event) {
  // This is called when connection events occur.
  // For reverse connections, we handle these events through RCConnectionWrapper.
  ENVOY_LOG(trace, "reverse_tunnel: event: {}", static_cast<int>(event));
}

int ReverseConnectionIOHandle::getPipeMonitorFd() const { return trigger_pipe_read_fd_; }

// Get time source for consistent time operations.
TimeSource& ReverseConnectionIOHandle::getTimeSource() const {
  // Try to get time source from thread-local dispatcher first.
  if (extension_) {
    auto* local_registry = extension_->getLocalRegistry();
    if (local_registry) {
      return local_registry->dispatcher().timeSource();
    }
  }

  // Fallback to worker dispatcher if available.
  if (worker_dispatcher_) {
    return worker_dispatcher_->timeSource();
  }

  // This should not happen in production. Assert to ensure proper initialization.
  ENVOY_BUG(false, "No time source available. dispatcher not properly initialized");
  PANIC("reverse_tunnel: No valid time source available");
}

// Use the thread-local registry to get the dispatcher.
Event::Dispatcher& ReverseConnectionIOHandle::getThreadLocalDispatcher() const {
  // Get the thread-local dispatcher from the socket interface's registry.
  auto* local_registry = extension_->getLocalRegistry();

  if (local_registry) {
    // Return the dispatcher from the thread-local registry.
    ENVOY_LOG(debug, "reverse_tunnel: dispatcher: {}", local_registry->dispatcher().name());
    return local_registry->dispatcher();
  }

  ENVOY_BUG(false, "Failed to get dispatcher from thread-local registry");
  // This should never happen in normal operation, but we need to handle it gracefully.
  RELEASE_ASSERT(worker_dispatcher_ != nullptr, "No dispatcher available");
  return *worker_dispatcher_;
}

// Safe wrapper for accessing thread-local dispatcher
bool ReverseConnectionIOHandle::isThreadLocalDispatcherAvailable() const {
  auto* local_registry = extension_->getLocalRegistry();
  return local_registry != nullptr;
}

ReverseTunnelInitiatorExtension* ReverseConnectionIOHandle::getDownstreamExtension() const {
  return extension_;
}

void ReverseConnectionIOHandle::maybeUpdateHostsMappingsAndConnections(
    const std::string& cluster_id, const std::vector<std::string>& hosts) {
  absl::flat_hash_set<std::string> new_hosts(hosts.begin(), hosts.end());
  absl::flat_hash_set<std::string> removed_hosts;
  const auto& cluster_to_resolved_hosts_itr = cluster_to_resolved_hosts_map_.find(cluster_id);
  if (cluster_to_resolved_hosts_itr != cluster_to_resolved_hosts_map_.end()) {
    // removed_hosts contains the hosts that were previously resolved.
    removed_hosts = cluster_to_resolved_hosts_itr->second;
  }
  for (const std::string& host : hosts) {
    if (removed_hosts.find(host) != removed_hosts.end()) {
      // Since the host still exists, we will remove it from removed_hosts.
      removed_hosts.erase(host);
    }
    ENVOY_LOG(debug, "Adding remote host {} to cluster {}", host, cluster_id);

    // Update or create host info.
    auto host_it = host_to_conn_info_map_.find(host);
    if (host_it == host_to_conn_info_map_.end()) {
      // Create host entry on-demand to avoid race conditions during host registration.
      ENVOY_LOG(
          debug,
          "Creating HostConnectionInfo on-demand during host update for host {} in cluster {}",
          host, cluster_id);
      host_to_conn_info_map_[host] = HostConnectionInfo{
          host,
          cluster_id,
          {},                              // connection_keys - empty set initially
          1,                               // default target_connection_count
          0,                               // failure_count
          getTimeSource().monotonicTime(), // last_failure_time
          getTimeSource().monotonicTime(), // backoff_until (no backoff initially)
          {}                               // connection_states - empty map initially
      };
    } else {
      // Update cluster name if host moved to different cluster.
      host_it->second.cluster_name = cluster_id;
    }
  }
  cluster_to_resolved_hosts_map_[cluster_id] = new_hosts;
  ENVOY_LOG(debug, "reverse_tunnel: Removing {} remote hosts from cluster {}", removed_hosts.size(),
            cluster_id);

  // Remove the hosts present in removed_hosts.
  for (const std::string& host : removed_hosts) {
    removeStaleHostAndCloseConnections(host);
    host_to_conn_info_map_.erase(host);
  }
}

void ReverseConnectionIOHandle::removeStaleHostAndCloseConnections(const std::string& host) {
  ENVOY_LOG(info, "reverse_tunnel: Removing all connections to remote host {}", host);
  // Find all wrappers for this host. Each wrapper represents a reverse connection to the host.
  std::vector<RCConnectionWrapper*> wrappers_to_remove;
  for (const auto& [wrapper, mapped_host] : conn_wrapper_to_host_map_) {
    if (mapped_host == host) {
      wrappers_to_remove.push_back(wrapper);
    }
  }
  ENVOY_LOG(info, "Found {} connections to remove for host {}", wrappers_to_remove.size(), host);
  // Remove wrappers and close connections.
  for (auto* wrapper : wrappers_to_remove) {
    ENVOY_LOG(debug, "Removing connection wrapper for host {}", host);

    // Get the connection from wrapper and close it.
    auto* connection = wrapper->getConnection();
    if (connection && connection->state() == Network::Connection::State::Open) {
      connection->close(Network::ConnectionCloseType::FlushWrite);
    }

    // Remove from wrapper-to-host map.
    conn_wrapper_to_host_map_.erase(wrapper);
    // Remove the wrapper from connection_wrappers_ vector.
    connection_wrappers_.erase(
        std::remove_if(connection_wrappers_.begin(), connection_wrappers_.end(),
                       [wrapper](const std::unique_ptr<RCConnectionWrapper>& w) {
                         return w.get() == wrapper;
                       }),
        connection_wrappers_.end());
  }
  // Clear connection keys from host info.
  auto host_it = host_to_conn_info_map_.find(host);
  if (host_it != host_to_conn_info_map_.end()) {
    host_it->second.connection_keys.clear();
  }
}

void ReverseConnectionIOHandle::maintainClusterConnections(
    const std::string& cluster_name, const RemoteClusterConnectionConfig& cluster_config) {
  ENVOY_LOG(debug,
            "reverse_tunnel: Maintaining connections for cluster: {} with {} requested "
            "connections per host",
            cluster_name, cluster_config.reverse_connection_count);

  // Generate a temporary connection key for early failure tracking, to update stats gauges.
  const std::string temp_connection_key = "temp_" + cluster_name + "_" + std::to_string(rand());

  // Get thread local cluster to access resolved hosts.
  auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "Cluster '{}' not found for reverse tunnel - will retry later", cluster_name);
    updateConnectionState("", cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return;
  }
  // Get all resolved hosts for the cluster.
  const auto& host_map_ptr = thread_local_cluster->prioritySet().crossPriorityHostMap();
  if (host_map_ptr == nullptr || host_map_ptr->empty()) {
    ENVOY_LOG(error, "No hosts found in cluster '{}' - will retry later", cluster_name);
    updateConnectionState("", cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return;
  }
  // Retrieve the resolved hosts for a cluster and update the corresponding maps.
  std::vector<std::string> resolved_hosts;
  for (const auto& host_itr : *host_map_ptr) {
    const std::string& resolved = host_itr.first;
    resolved_hosts.emplace_back(resolved);
  }
  maybeUpdateHostsMappingsAndConnections(cluster_name, std::move(resolved_hosts));
  // Track successful connections for this cluster.
  uint32_t total_successful_connections = 0;
  uint32_t total_required_connections =
      host_map_ptr->size() * cluster_config.reverse_connection_count;

  // Create connections to each host in the cluster.
  for (const auto& [host_address, host] : *host_map_ptr) {
    ENVOY_LOG(debug, "reverse_tunnel: Checking reverse connection count for host {} of cluster {}",
              host_address, cluster_name);

    // Ensure HostConnectionInfo exists for this host, handling internal addresses consistently.
    const std::string key = host_address;
    auto host_it = host_to_conn_info_map_.find(key);
    if (host_it == host_to_conn_info_map_.end()) {
      ENVOY_LOG(debug, "Creating HostConnectionInfo for host {} in cluster {}", key, cluster_name);
      host_to_conn_info_map_[key] = HostConnectionInfo{
          key,
          cluster_name,
          {},                                      // connection_keys - empty set initially
          cluster_config.reverse_connection_count, // target_connection_count from config
          0,                                       // failure_count
          // last_failure_time
          worker_dispatcher_->timeSource().monotonicTime(),
          // backoff_until
          worker_dispatcher_->timeSource().monotonicTime(),
          {} // connection_states
      };
    }

    // Check if we should attempt connection to this host (backoff logic).
    if (!shouldAttemptConnectionToHost(host_address, cluster_name)) {
      ENVOY_LOG(debug, "reverse_tunnel: Skipping connection attempt to host {} due to backoff",
                host_address);
      continue;
    }
    // Get current number of successful connections to this host.
    uint32_t current_connections = host_to_conn_info_map_[key].connection_keys.size();
    uint32_t pending_connections = host_to_conn_info_map_[key].connecting_count;

    ENVOY_LOG(info,
              "reverse_tunnel: Number of reverse connections to host {} of cluster {}: "
              "Current: {}, Pending: {}, Required: {}",
              host_address, cluster_name, current_connections, pending_connections,
              cluster_config.reverse_connection_count);
    // Update with the pending connections also for checking against required.
    current_connections += pending_connections;
    if (current_connections >= cluster_config.reverse_connection_count) {
      ENVOY_LOG(debug,
                "reverse_tunnel: No more reverse connections needed to host {} of cluster {}",
                host_address, cluster_name);
      total_successful_connections += current_connections;
      continue;
    }
    const uint32_t needed_connections =
        cluster_config.reverse_connection_count - current_connections;

    ENVOY_LOG(debug,
              "reverse_tunnel: Initiating {} reverse connections to host {} of remote "
              "cluster '{}' from source node '{}'",
              needed_connections, host_address, cluster_name, config_.src_node_id);
    // Create the required number of connections to this specific host.
    for (uint32_t i = 0; i < needed_connections; ++i) {
      ENVOY_LOG(debug, "Initiating reverse connection number {} to host {} of cluster {}", i + 1,
                host_address, cluster_name);

      bool success = initiateOneReverseConnection(cluster_name, key, host);

      if (success) {
        total_successful_connections++;
        ENVOY_LOG(debug,
                  "Successfully initiated reverse connection number {} to host {} of cluster {}",
                  i + 1, host_address, cluster_name);
      } else {
        ENVOY_LOG(error, "Failed to initiate reverse connection number {} to host {} of cluster {}",
                  i + 1, host_address, cluster_name);
      }
    }
  }
  // Update metrics based on overall success for the cluster.
  if (total_successful_connections > 0) {
    ENVOY_LOG(info,
              "reverse_tunnel: Successfully created {}/{} total reverse connections to "
              "cluster {}",
              total_successful_connections, total_required_connections, cluster_name);
  } else {
    ENVOY_LOG(error,
              "reverse_tunnel: Failed to create any reverse connections to cluster {} - "
              "will retry later",
              cluster_name);
  }
}

bool ReverseConnectionIOHandle::shouldAttemptConnectionToHost(const std::string& host_address,
                                                              const std::string& cluster_name) {
  if (!config_.enable_circuit_breaker) {
    return true;
  }
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it == host_to_conn_info_map_.end()) {
    // Create host entry on-demand to avoid race conditions during initialization.
    ENVOY_LOG(debug, "Creating HostConnectionInfo on-demand for host {} in cluster {}",
              host_address, cluster_name);
    host_to_conn_info_map_[host_address] = HostConnectionInfo{
        host_address,
        cluster_name,
        {},                              // connection_keys - empty set initially
        1,                               // default target_connection_count
        0,                               // failure_count
        getTimeSource().monotonicTime(), // last_failure_time
        getTimeSource().monotonicTime(), // backoff_until (no backoff initially)
        {}                               // connection_states - empty map initially
    };
    host_it = host_to_conn_info_map_.find(host_address);
  }
  auto& host_info = host_it->second;
  auto now = getTimeSource().monotonicTime();
  ENVOY_LOG(debug, "host: {} now: {} ms backoff_until: {} ms", host_address,
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                host_info.backoff_until.time_since_epoch())
                .count());
  // Check if we're still in backoff period.
  if (now < host_info.backoff_until) {
    auto remaining_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(host_info.backoff_until - now)
            .count();
    ENVOY_LOG(debug, "reverse_tunnel: Host {} still in backoff for {}ms", host_address,
              remaining_ms);
    return false;
  }
  return true;
}

void ReverseConnectionIOHandle::trackConnectionFailure(const std::string& host_address,
                                                       const std::string& cluster_name) {
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it == host_to_conn_info_map_.end()) {
    ENVOY_LOG(debug, "Host {} not found in host_to_conn_info_map_, skipping failure tracking",
              host_address);
    return;
  }
  auto& host_info = host_it->second;
  host_info.failure_count++;
  host_info.last_failure_time = getTimeSource().monotonicTime();
  // Calculate exponential backoff: base_delay * 2^(failure_count - 1)
  const uint32_t base_delay_ms = 1000; // 1 second base delay
  const uint32_t max_delay_ms = 30000; // 30 seconds max delay

  uint32_t backoff_delay_ms = base_delay_ms * (1 << (host_info.failure_count - 1));
  backoff_delay_ms = std::min(backoff_delay_ms, max_delay_ms);
  // Update the backoff until time. This is used in shouldAttemptConnectionToHost() to check if we
  // should attempt to connect to the host.
  host_info.backoff_until =
      host_info.last_failure_time + std::chrono::milliseconds(backoff_delay_ms);

  ENVOY_LOG(debug, "Host {} connection failure #{}, backoff until {}ms from now", host_address,
            host_info.failure_count, backoff_delay_ms);

  // Mark host as in backoff state using host+cluster as connection key. For backoff, the connection
  // key does not matter since we just need to mark the host and cluster that are in backoff state
  // for.
  const std::string backoff_connection_key = host_address + "_" + cluster_name + "_backoff";
  updateConnectionState(host_address, cluster_name, backoff_connection_key,
                        ReverseConnectionState::Backoff);
  ENVOY_LOG(debug, "reverse_tunnel: Marked host {} in cluster {} as Backoff with connection key {}",
            host_address, cluster_name, backoff_connection_key);
}

void ReverseConnectionIOHandle::resetHostBackoff(const std::string& host_address) {
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it == host_to_conn_info_map_.end()) {
    ENVOY_LOG(debug, "Host {} not found in host_to_conn_info_map_, skipping backoff reset",
              host_address);
    return;
  }

  auto& host_info = host_it->second;
  auto now = getTimeSource().monotonicTime();

  // Check if the host is actually in backoff before resetting.
  if (now >= host_info.backoff_until) {
    ENVOY_LOG(debug, "Host {} is not in backoff, skipping reset", host_address);
    return;
  }

  host_info.failure_count = 0;
  host_info.backoff_until = getTimeSource().monotonicTime();
  ENVOY_LOG(debug, "reverse_tunnel: Reset backoff for host {}", host_address);

  // Mark host as recovered using the same key used by backoff to change the state from backoff to
  // recovered.
  const std::string recovered_connection_key =
      host_address + "_" + host_info.cluster_name + "_backoff";
  updateConnectionState(host_address, host_info.cluster_name, recovered_connection_key,
                        ReverseConnectionState::Recovered);
  ENVOY_LOG(debug,
            "reverse_tunnel: Marked host {} in cluster {} as Recovered with connection key {}",
            host_address, host_info.cluster_name, recovered_connection_key);
}

void ReverseConnectionIOHandle::updateConnectionState(const std::string& host_address,
                                                      const std::string& cluster_name,
                                                      const std::string& connection_key,
                                                      ReverseConnectionState new_state) {
  // Update connection state in host info and handle old state.
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it != host_to_conn_info_map_.end()) {
    // Increment first and then decrement if needed.
    if (new_state == ReverseConnectionState::Connecting) {
      host_it->second.connecting_count++;
    }

    // Remove old connection state if it exists.
    auto old_state_it = host_it->second.connection_states.find(connection_key);
    if (old_state_it != host_it->second.connection_states.end()) {
      ReverseConnectionState old_state = old_state_it->second;
      if (old_state == ReverseConnectionState::Connecting) {
        host_it->second.connecting_count--;
      }
      // Decrement old state gauge using unified function.
      updateStateGauge(host_address, cluster_name, old_state, false /* decrement */);
    }

    // Set new connection state.
    host_it->second.connection_states[connection_key] = new_state;
  }

  // Increment new state gauge using unified function.
  updateStateGauge(host_address, cluster_name, new_state, true /* increment */);

  ENVOY_LOG(debug,
            "ReverseConnectionIOHandle:Updated connection {} state to {} for host {} in cluster {}",
            connection_key, static_cast<int>(new_state), host_address, cluster_name);
}

void ReverseConnectionIOHandle::removeConnectionState(const std::string& host_address,
                                                      const std::string& cluster_name,
                                                      const std::string& connection_key) {
  // Remove connection state from host info and decrement gauge.
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it != host_to_conn_info_map_.end()) {
    auto state_it = host_it->second.connection_states.find(connection_key);
    if (state_it != host_it->second.connection_states.end()) {
      ReverseConnectionState old_state = state_it->second;
      // Decrement state gauge using unified function.
      updateStateGauge(host_address, cluster_name, old_state, false /* decrement */);
      // Remove gauge from map.
      host_it->second.connection_states.erase(state_it);
    }
  }

  ENVOY_LOG(debug, "reverse_tunnel: Removed connection {} state for host {} in cluster {}",
            connection_key, host_address, cluster_name);
}

void ReverseConnectionIOHandle::onDownstreamConnectionClosed(const std::string& connection_key) {
  ENVOY_LOG(debug, "reverse_tunnel: Downstream connection closed: {}", connection_key);

  // Find the host for this connection key.
  std::string host_address;
  std::string cluster_name;

  // Search through host_to_conn_info_map_ to find which host this connection belongs to.
  for (const auto& [host, host_info] : host_to_conn_info_map_) {
    if (host_info.connection_keys.find(connection_key) != host_info.connection_keys.end()) {
      host_address = host;
      cluster_name = host_info.cluster_name;
      break;
    }
  }

  if (host_address.empty()) {
    ENVOY_LOG(warn, "Could not find host for connection key: {}", connection_key);
    return;
  }

  ENVOY_LOG(debug, "Found connection {} belongs to host {} in cluster {}", connection_key,
            host_address, cluster_name);

  // Remove the connection key from the host's connection set.
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it != host_to_conn_info_map_.end()) {
    host_it->second.connection_keys.erase(connection_key);
    ENVOY_LOG(debug, "Removed connection key {} from host {} (remaining: {})", connection_key,
              host_address, host_it->second.connection_keys.size());
  }

  // Remove connection state tracking.
  removeConnectionState(host_address, cluster_name, connection_key);

  // The next call to maintainClusterConnections() will detect the missing connection
  // and re-initiate it automatically.
  ENVOY_LOG(debug,
            "reverse_tunnel: Connection closure recorded for host {} in cluster {}. "
            "Next maintenance cycle will re-initiate if needed.",
            host_address, cluster_name);
}

void ReverseConnectionIOHandle::updateStateGauge(const std::string& host_address,
                                                 const std::string& cluster_name,
                                                 ReverseConnectionState state, bool increment) {
  // Get extension for stats updates.
  auto* extension = getDownstreamExtension();
  if (!extension) {
    ENVOY_LOG(debug, "No downstream extension available for state gauge update");
    return;
  }

  // Use switch case to determine the state suffix for stat name.
  std::string state_suffix;
  switch (state) {
  case ReverseConnectionState::Connecting:
    state_suffix = "connecting";
    break;
  case ReverseConnectionState::Connected:
    state_suffix = "connected";
    break;
  case ReverseConnectionState::Failed:
    state_suffix = "failed";
    break;
  case ReverseConnectionState::Recovered:
    state_suffix = "recovered";
    break;
  case ReverseConnectionState::Backoff:
    state_suffix = "backoff";
    break;
  case ReverseConnectionState::CannotConnect:
    state_suffix = "cannot_connect";
    break;
  default:
    state_suffix = "unknown";
    break;
  }

  // Call extension to handle the actual stat update.
  extension_->updateConnectionStats(host_address, cluster_name, state_suffix, increment);

  ENVOY_LOG(trace, "{} state gauge for host {} cluster {} state {}",
            increment ? "Incremented" : "Decremented", host_address, cluster_name, state_suffix);
}

void ReverseConnectionIOHandle::maintainReverseConnections() {
  // Validate required configuration parameters at the top level.
  if (config_.src_node_id.empty()) {
    ENVOY_LOG(error, "Source node ID is required but empty - cannot maintain reverse connections");
    return;
  }

  ENVOY_LOG(debug, "Maintaining reverse tunnels for {} clusters.", config_.remote_clusters.size());
  for (const auto& cluster_config : config_.remote_clusters) {
    const std::string& cluster_name = cluster_config.cluster_name;

    ENVOY_LOG(debug, "Processing cluster: {} with {} requested connections per host.", cluster_name,
              cluster_config.reverse_connection_count);
    // Maintain connections for this cluster.
    maintainClusterConnections(cluster_name, cluster_config);
  }
  ENVOY_LOG(debug, "Completed reverse TCP connection maintenance for all clusters.");

  // Enable the retry timer to periodically check for missing connections (like maintainConnCount)
  if (rev_conn_retry_timer_) {
    // TODO(basundhara-c): Make the retry timeout configurable.
    const std::chrono::milliseconds retry_timeout(10000); // 10 seconds
    rev_conn_retry_timer_->enableTimer(retry_timeout);
    ENVOY_LOG(debug, "Enabled retry timer for next connection check in 10 seconds.");
  }
}

bool ReverseConnectionIOHandle::initiateOneReverseConnection(const std::string& cluster_name,
                                                             const std::string& host_address,
                                                             Upstream::HostConstSharedPtr host) {
  // Generate a temporary connection key for early failure tracking.
  const std::string temp_connection_key =
      "temp_" + cluster_name + "_" + host_address + "_" + std::to_string(rand());

  // Only validate host_address here since it's specific to this connection attempt.
  if (host_address.empty()) {
    ENVOY_LOG(error, "Host address is required but empty");
    updateConnectionState(host_address, cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return false;
  }

  ENVOY_LOG(debug,
            "reverse_tunnel: Initiating one reverse connection to host {} of cluster "
            "'{}', source node '{}'",
            host_address, cluster_name, config_.src_node_id);
  // Get the thread local cluster with additional validation.
  auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "Cluster '{}' not found in cluster manager", cluster_name);
    updateConnectionState(host_address, cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return false;
  }

  // Validate cluster info before attempting connection.
  auto cluster_info = thread_local_cluster->info();
  if (!cluster_info) {
    ENVOY_LOG(error, "Cluster '{}' has null cluster info", cluster_name);
    updateConnectionState(host_address, cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return false;
  }

  // Validate priority set to prevent null pointer access.
  const auto& priority_set = thread_local_cluster->prioritySet();
  const auto& host_sets = priority_set.hostSetsPerPriority();
  size_t host_count = host_sets.empty() ? 0 : host_sets[0]->hosts().size();
  ENVOY_LOG(debug, "reverse_tunnel: Cluster '{}' found with type {} and {} hosts", cluster_name,
            static_cast<int>(cluster_info->type()), host_count);

  // Normalize host key for internal addresses to ensure consistent map lookups.
  std::string normalized_host_key = host_address;
  if (absl::StartsWith(host_address, "envoy://")) {
    normalized_host_key = host_address; // already canonical for internal addresses
  }

  // Validate that we have hosts available for internal addresses.
  if (absl::StartsWith(host_address, "envoy://") && host_count == 0) {
    ENVOY_LOG(error, "reverse_tunnel: No hosts available in cluster '{}' for internal address '{}'",
              cluster_name, host_address);
    updateConnectionState(host_address, cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return false;
  }

  // Create load balancer context and validate it.
  ReverseConnectionLoadBalancerContext lb_context(normalized_host_key);
  ENVOY_LOG(debug, "reverse_tunnel: Created load balancer context for host key: {}",
            normalized_host_key);

  // Get connection from cluster manager with defensive error handling.
  Upstream::Host::CreateConnectionData conn_data;
  ENVOY_LOG(debug,
            "reverse_tunnel: Creating TCP connection to {} in cluster {} using load "
            "balancer context",
            host_address, cluster_name);

  // Use tcpConn which should not throw exceptions in normal operation.
  conn_data = thread_local_cluster->tcpConn(&lb_context);

  if (!conn_data.connection_) {
    ENVOY_LOG(error, "reverse_tunnel: tcpConn() returned null connection for host {} in cluster {}",
              host_address, cluster_name);
    updateConnectionState(host_address, cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return false;
  }

  // Create wrapper to manage the connection.
  // The wrapper will initiate and manage the reverse connection handshake using HTTP.
  auto wrapper = std::make_unique<RCConnectionWrapper>(*this, std::move(conn_data.connection_),
                                                       conn_data.host_description_, cluster_name);

  // Send the reverse connection handshake over the TCP connection.
  const std::string connection_key =
      wrapper->connect(config_.src_tenant_id, config_.src_cluster_id, config_.src_node_id);
  ENVOY_LOG(debug, "reverse_tunnel: Initiated reverse connection handshake for host {} with key {}",
            host_address, connection_key);

  // Mark as Connecting after handshake is initiated. Use the actual connection key so that it can
  // be marked as failed in onConnectionDone().
  conn_wrapper_to_host_map_[wrapper.get()] = normalized_host_key;
  connection_wrappers_.push_back(std::move(wrapper));

  {
    // Safely log address information without assuming IP is present (internal addresses possible).
    const auto& addr = host->address();
    std::string addr_str = addr ? addr->asString() : std::string("<unknown>");
    absl::optional<uint16_t> port_opt;
    if (addr && addr->ip() != nullptr) {
      port_opt = addr->ip()->port();
    }
    if (port_opt.has_value()) {
      ENVOY_LOG(debug,
                "reverse_tunnel: Successfully initiated reverse connection to host {} "
                "({}:{}) in cluster {}",
                host_address, addr_str, *port_opt, cluster_name);
    } else {
      ENVOY_LOG(debug,
                "reverse_tunnel: Successfully initiated reverse connection to host {} "
                "({}) in cluster {}",
                host_address, addr_str, cluster_name);
    }
  }
  // Reset backoff for successful connection.
  resetHostBackoff(normalized_host_key);
  updateConnectionState(normalized_host_key, cluster_name, connection_key,
                        ReverseConnectionState::Connecting);
  return true;
}

// Trigger pipe used to wake up accept() when a connection is established.
void ReverseConnectionIOHandle::createTriggerPipe() {
  ENVOY_LOG(debug, "reverse_tunnel: Creating trigger pipe for single-byte mechanism");
  int pipe_fds[2];
  if (pipe(pipe_fds) == -1) {
    ENVOY_LOG(error, "Failed to create trigger pipe: {}", errorDetails(errno));
    trigger_pipe_read_fd_ = -1;
    trigger_pipe_write_fd_ = -1;
    return;
  }
  trigger_pipe_read_fd_ = pipe_fds[0];
  trigger_pipe_write_fd_ = pipe_fds[1];
  // Make both ends non-blocking.
  int flags = fcntl(trigger_pipe_write_fd_, F_GETFL, 0);
  if (flags != -1) {
    fcntl(trigger_pipe_write_fd_, F_SETFL, flags | O_NONBLOCK);
  }
  flags = fcntl(trigger_pipe_read_fd_, F_GETFL, 0);
  if (flags != -1) {
    fcntl(trigger_pipe_read_fd_, F_SETFL, flags | O_NONBLOCK);
  }
  ENVOY_LOG(debug, "reverse_tunnel: Created trigger pipe: read_fd={}, write_fd={}",
            trigger_pipe_read_fd_, trigger_pipe_write_fd_);
}

bool ReverseConnectionIOHandle::isTriggerPipeReady() const {
  return trigger_pipe_read_fd_ != -1 && trigger_pipe_write_fd_ != -1;
}

void ReverseConnectionIOHandle::onConnectionDone(const std::string& error,
                                                 RCConnectionWrapper* wrapper, bool closed) {
  ENVOY_LOG(debug, "reverse_tunnel: Connection wrapper done - error: '{}', closed: {}", error,
            closed);

  // Validate wrapper pointer before any access.
  if (!wrapper) {
    ENVOY_LOG(error, "reverse_tunnel: Null wrapper pointer in onConnectionDone");
    return;
  }

  std::string host_address;
  std::string cluster_name;
  std::string connection_key;

  // Safely get host address for wrapper.
  auto wrapper_it = conn_wrapper_to_host_map_.find(wrapper);
  if (wrapper_it == conn_wrapper_to_host_map_.end()) {
    ENVOY_LOG(error, "reverse_tunnel: Wrapper not found in conn_wrapper_to_host_map_ - "
                     "may have been cleaned up");
    return;
  }
  host_address = wrapper_it->second;

  // Safely get cluster name from host info.
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it != host_to_conn_info_map_.end()) {
    cluster_name = host_it->second.cluster_name;
  } else {
    ENVOY_LOG(warn, "reverse_tunnel: Host info not found for {}, using fallback", host_address);
  }

  if (cluster_name.empty()) {
    ENVOY_LOG(error,
              "reverse_tunnel: No cluster mapping for host {}, cannot process "
              "connection event",
              host_address);
    // Still try to clean up the wrapper.
    conn_wrapper_to_host_map_.erase(wrapper);
    return;
  }

  // Safely get connection info if wrapper is still valid.
  auto* connection = wrapper->getConnection();
  if (connection) {
    connection_key = connection->connectionInfoProvider().localAddress()->asString();
    ENVOY_LOG(debug,
              "reverse_tunnel: Processing connection event for host '{}', cluster "
              "'{}', key '{}'",
              host_address, cluster_name, connection_key);
  } else {
    connection_key = "cleanup_" + host_address + "_" + std::to_string(rand());
    ENVOY_LOG(debug, "reverse_tunnel: Connection already null, using fallback key '{}'",
              connection_key);
  }

  // Get connection pointer for safe access in success/failure handling.
  connection = wrapper->getConnection();

  // Process connection result safely.
  bool is_success = (error == "reverse connection accepted" || error == "success" ||
                     error == "handshake successful" || error == "connection established");

  if (closed || (!error.empty() && !is_success)) {
    // Handle connection failure.
    ENVOY_LOG(error, "reverse_tunnel: Connection failed - error '{}', cleaning up host {}", error,
              host_address);

    updateConnectionState(host_address, cluster_name, connection_key,
                          ReverseConnectionState::Failed);

    // Safely close connection if still valid.
    if (connection) {
      if (connection->getSocket()) {
        connection->getSocket()->ioHandle().resetFileEvents();
      }
      connection->close(Network::ConnectionCloseType::NoFlush);
    }

    trackConnectionFailure(host_address, cluster_name);

  } else {
    // Handle connection success.
    ENVOY_LOG(debug, "reverse_tunnel: Connection succeeded for host {}", host_address);

    resetHostBackoff(host_address);
    updateConnectionState(host_address, cluster_name, connection_key,
                          ReverseConnectionState::Connected);

    // Only proceed if connection is still valid.
    if (!connection) {
      ENVOY_LOG(error, "reverse_tunnel: Cannot complete successful handshake - connection is null");
      return;
    }

    ENVOY_LOG(info, "reverse_tunnel: Transferring tunnel socket for "
                    "reverse_conn_listener consumption");

    // Reset file events safely.
    if (connection->getSocket()) {
      ENVOY_LOG(debug, "reverse_tunnel: Removing connection callbacks and resetting file events");
      connection->removeConnectionCallbacks(*wrapper);
      connection->getSocket()->ioHandle().resetFileEvents();
    }

    // Update host connection tracking safely.
    auto host_it = host_to_conn_info_map_.find(host_address);
    if (host_it != host_to_conn_info_map_.end()) {
      host_it->second.connection_keys.insert(connection_key);
      ENVOY_LOG(debug, "reverse_tunnel: Added connection key {} for host {}", connection_key,
                host_address);
    }

    // Set quiet shutdown since we are duplicating the socket and closing the original socket. When
    // the original socket is closed, a TLS close_notify alert is otherwise sent.
    if (connection->ssl()) {
      ENVOY_LOG(
          trace,
          "reverse_tunnel: Setting quiet shutdown on SSL connection to prevent close_notify alert");
      const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_handshaker =
          dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
              connection->ssl().get());
      if (ssl_handshaker && ssl_handshaker->ssl()) {
        SSL_set_quiet_shutdown(ssl_handshaker->ssl(), 1);
        ENVOY_LOG(trace, "reverse_tunnel: Quiet shutdown enabled for connection {}",
                  connection_key);
      } else {
        ENVOY_LOG(warn,
                  "reverse_tunnel: Failed to cast to SslHandshakerImpl or ssl() returned null");
      }
    }

    Network::ClientConnectionPtr released_conn = wrapper->releaseConnection();

    if (released_conn) {
      ENVOY_LOG(info, "reverse_tunnel: Connection will be consumed by "
                      "reverse_conn_listener for HTTP processing");

      // Move connection to established queue for reverse_conn_listener to consume.
      established_connections_.push(std::move(released_conn));

      // Trigger accept mechanism safely.
      if (isTriggerPipeReady()) {
        char trigger_byte = 1;
        ssize_t bytes_written = ::write(trigger_pipe_write_fd_, &trigger_byte, 1);
        if (bytes_written == 1) {
          ENVOY_LOG(info,
                    "reverse_tunnel: Successfully triggered reverse_conn_listener "
                    "accept() for host {}",
                    host_address);
        } else {
          ENVOY_LOG(error, "reverse_tunnel: Failed to write trigger byte: {}", errorDetails(errno));
        }
      }
    }
  }

  // Safely remove wrapper from tracking.
  conn_wrapper_to_host_map_.erase(wrapper);

  // Find and remove wrapper from vector safely.
  auto wrapper_vector_it = std::find_if(
      connection_wrappers_.begin(), connection_wrappers_.end(),
      [wrapper](const std::unique_ptr<RCConnectionWrapper>& w) { return w.get() == wrapper; });

  if (wrapper_vector_it != connection_wrappers_.end()) {
    auto wrapper_to_delete = std::move(*wrapper_vector_it);
    connection_wrappers_.erase(wrapper_vector_it);

    // Use deferred deletion to prevent crash during cleanup.
    std::unique_ptr<Event::DeferredDeletable> deletable_wrapper(
        static_cast<Event::DeferredDeletable*>(wrapper_to_delete.release()));
    getThreadLocalDispatcher().deferredDelete(std::move(deletable_wrapper));
    ENVOY_LOG(debug, "reverse_tunnel: Deferred delete of connection wrapper");
  }
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
