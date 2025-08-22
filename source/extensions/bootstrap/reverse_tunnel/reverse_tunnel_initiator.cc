#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_initiator.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "envoy/event/deferred_deletable.h"
#include "envoy/extensions/bootstrap/reverse_connection_handshake/v3/reverse_connection_handshake.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"
#include "envoy/tracing/tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_connection_address.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// DownstreamReverseConnectionIOHandle constructor implementation
DownstreamReverseConnectionIOHandle::DownstreamReverseConnectionIOHandle(
    Network::ConnectionSocketPtr socket, ReverseConnectionIOHandle* parent,
    const std::string& connection_key)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), owned_socket_(std::move(socket)),
      parent_(parent), connection_key_(connection_key) {
  ENVOY_LOG(debug,
            "DownstreamReverseConnectionIOHandle: taking ownership of socket with FD: {} for "
            "connection key: {}",
            fd_, connection_key_);
}

// DownstreamReverseConnectionIOHandle destructor implementation
DownstreamReverseConnectionIOHandle::~DownstreamReverseConnectionIOHandle() {
  ENVOY_LOG(
      debug,
      "DownstreamReverseConnectionIOHandle: destroying handle for FD: {} with connection key: {}",
      fd_, connection_key_);
}

// DownstreamReverseConnectionIOHandle close() implementation
Api::IoCallUint64Result DownstreamReverseConnectionIOHandle::close() {
  ENVOY_LOG(
      debug,
      "DownstreamReverseConnectionIOHandle: closing handle for FD: {} with connection key: {}", fd_,
      connection_key_);

  // Prevent double-closing by checking if already closed
  if (fd_ < 0) {
    ENVOY_LOG(debug,
              "DownstreamReverseConnectionIOHandle: handle already closed for connection key: {}",
              connection_key_);
    return Api::ioCallUint64ResultNoError();
  }

  // Notify parent that this downstream connection has been closed
  // This will trigger re-initiation of the reverse connection if needed.
  if (parent_) {
    parent_->onDownstreamConnectionClosed(connection_key_);
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: notified parent of connection closure for key: {}",
        connection_key_);
  }

  // Reset the owned socket to properly close the connection.
  if (owned_socket_) {
    owned_socket_.reset();
  }
  return IoSocketHandleImpl::close();
}

// Forward declaration.
class ReverseConnectionIOHandle;
class ReverseTunnelInitiator;

// RCConnectionWrapper constructor implementation
RCConnectionWrapper::RCConnectionWrapper(ReverseConnectionIOHandle& parent,
                                         Network::ClientConnectionPtr connection,
                                         Upstream::HostDescriptionConstSharedPtr host,
                                         const std::string& cluster_name)
    : parent_(parent), connection_(std::move(connection)), host_(std::move(host)),
      cluster_name_(cluster_name) {
  ENVOY_LOG(debug, "RCConnectionWrapper: Using HTTP handshake for reverse connections");
}

// RCConnectionWrapper destructor implementation
RCConnectionWrapper::~RCConnectionWrapper() {
  ENVOY_LOG(debug, "RCConnectionWrapper destructor called");
  shutdown();
}

void RCConnectionWrapper::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    if (!connection_) {
      ENVOY_LOG(debug, "RCConnectionWrapper: connection is null, skipping event handling");
      return;
    }

    // Store connection info before it gets invalidated.
    const std::string connectionKey =
        connection_->connectionInfoProvider().localAddress()->asString();
    const uint64_t connectionId = connection_->id();

    ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, found connection {} remote closed",
              connectionId, connectionKey);

    // Don't call shutdown() here as it may cause cleanup during event processing
    // Instead, just notify parent of closure.
    parent_.onConnectionDone("Connection closed", this, true);
  }
}

// SimpleConnReadFilter::onData implementation
Network::FilterStatus RCConnectionWrapper::SimpleConnReadFilter::onData(Buffer::Instance& buffer,
                                                                        bool) {
  if (parent_ == nullptr) {
    ENVOY_LOG(error, "SimpleConnReadFilter: RCConnectionWrapper is null. Aborting read.");
    return Network::FilterStatus::StopIteration;
  }

  const std::string data = buffer.toString();
  ENVOY_LOG(debug, "SimpleConnReadFilter: Received data: {}", data);

  // Look for HTTP response status line first (supports both HTTP/1.1 and HTTP/2)
  if (data.find("HTTP/1.1 200 OK") != std::string::npos ||
      data.find("HTTP/2 200") != std::string::npos) {
    ENVOY_LOG(debug, "Received HTTP 200 OK response");

    // Find the end of headers (double CRLF)
    size_t headers_end = data.find("\r\n\r\n");
    if (headers_end != std::string::npos) {
      // Extract the response body (after headers)
      std::string response_body = data.substr(headers_end + 4);

      if (!response_body.empty()) {
        // Try to parse the protobuf response
        envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeRet ret;
        if (ret.ParseFromString(response_body)) {
          ENVOY_LOG(debug, "Successfully parsed protobuf response: {}", ret.DebugString());

          // Check if the status is ACCEPTED
          if (ret.status() == envoy::extensions::bootstrap::reverse_connection_handshake::v3::
                                  ReverseConnHandshakeRet::ACCEPTED) {
            ENVOY_LOG(debug, "SimpleConnReadFilter: Reverse connection accepted by cloud side");
            parent_->onHandshakeSuccess();
            return Network::FilterStatus::StopIteration;
          } else {
            ENVOY_LOG(error, "SimpleConnReadFilter: Reverse connection rejected: {}",
                      ret.status_message());
            parent_->onHandshakeFailure(ret.status_message());
            return Network::FilterStatus::StopIteration;
          }
        } else {
          ENVOY_LOG(error, "Could not parse protobuf response - invalid response format");
          parent_->onHandshakeFailure(
              "Invalid response format - expected ReverseConnHandshakeRet protobuf");
          return Network::FilterStatus::StopIteration;
        }
      } else {
        ENVOY_LOG(debug, "Response body is empty, waiting for more data");
        return Network::FilterStatus::Continue;
      }
    } else {
      ENVOY_LOG(debug, "HTTP headers not complete yet, waiting for more data");
      return Network::FilterStatus::Continue;
    }
  } else if (data.find("HTTP/1.1 ") != std::string::npos ||
             data.find("HTTP/2 ") != std::string::npos) {
    // Found HTTP response but not 200 OK - this is an error
    ENVOY_LOG(error, "Received non-200 HTTP response: {}", data.substr(0, 100));
    parent_->onHandshakeFailure("HTTP handshake failed with non-200 response");
    return Network::FilterStatus::StopIteration;
  } else {
    ENVOY_LOG(debug, "Waiting for HTTP response, received {} bytes", data.length());
    return Network::FilterStatus::Continue;
  }
}

std::string RCConnectionWrapper::connect(const std::string& src_tenant_id,
                                         const std::string& src_cluster_id,
                                         const std::string& src_node_id) {
  // Register connection callbacks.
  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, adding connection callbacks",
            connection_->id());
  connection_->addConnectionCallbacks(*this);
  connection_->connect();

  // Use HTTP handshake
  ENVOY_LOG(debug,
            "RCConnectionWrapper: connection: {}, sending reverse connection creation "
            "request through HTTP",
            connection_->id());

  // Add read filter to handle HTTP response
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new SimpleConnReadFilter(this)});

  // Use HTTP handshake logic
  envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeArg arg;
  arg.set_tenant_uuid(src_tenant_id);
  arg.set_cluster_uuid(src_cluster_id);
  arg.set_node_uuid(src_node_id);
  ENVOY_LOG(debug,
            "RCConnectionWrapper: Creating protobuf with tenant='{}', cluster='{}', node='{}'",
            src_tenant_id, src_cluster_id, src_node_id);
  std::string body = arg.SerializeAsString(); // NOLINT(protobuf-use-MessageUtil-hash)
  ENVOY_LOG(debug, "RCConnectionWrapper: Serialized protobuf body length: {}, debug: '{}'",
            body.length(), arg.DebugString());
  std::string host_value;
  const auto& remote_address = connection_->connectionInfoProvider().remoteAddress();
  // This is used when reverse connections need to be established through a HTTP proxy.
  // The reverse connection listener connects to an internal cluster, to which an
  // internal listener listens. This internal listener has tunneling configuration
  // to tcp proxy the reverse connection requests over HTTP/1 CONNECT to the remote
  // proxy.
  if (remote_address->type() == Network::Address::Type::EnvoyInternal) {
    const auto& internal_address =
        std::dynamic_pointer_cast<const Network::Address::EnvoyInternalInstance>(remote_address);
    ENVOY_LOG(debug,
              "RCConnectionWrapper: connection: {}, remote address is internal "
              "listener {}, using endpoint ID in host header",
              connection_->id(), internal_address->envoyInternalAddress()->addressId());
    host_value = internal_address->envoyInternalAddress()->endpointId();
  } else {
    host_value = remote_address->asString();
    ENVOY_LOG(debug,
              "RCConnectionWrapper: connection: {}, remote address is external, "
              "using address as host header",
              connection_->id());
  }
  // Build HTTP request with protobuf body.
  Buffer::OwnedImpl reverse_connection_request(
      fmt::format("POST /reverse_connections/request HTTP/1.1\r\n"
                  "Host: {}\r\n"
                  "Accept: */*\r\n"
                  "Content-length: {}\r\n"
                  "\r\n{}",
                  host_value, body.length(), body));
  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, writing request to connection: {}",
            connection_->id(), reverse_connection_request.toString());
  // Send reverse connection request over TCP connection.
  connection_->write(reverse_connection_request, false);

  return connection_->connectionInfoProvider().localAddress()->asString();
}

void RCConnectionWrapper::onHandshakeSuccess() {
  std::string message = "reverse connection accepted";
  ENVOY_LOG(debug, "handshake succeeded: {}", message);
  parent_.onConnectionDone(message, this, false);
}

void RCConnectionWrapper::onHandshakeFailure(const std::string& message) {
  ENVOY_LOG(debug, "handshake failed: {}", message);
  parent_.onConnectionDone(message, this, false);
}

void RCConnectionWrapper::shutdown() {
  if (!connection_) {
    ENVOY_LOG(error, "RCConnectionWrapper: Connection already null, nothing to shutdown");
    return;
  }

  ENVOY_LOG(debug, "RCConnectionWrapper: Shutting down connection ID: {}, state: {}",
            connection_->id(), static_cast<int>(connection_->state()));

  // Remove connection callbacks first to prevent recursive calls during shutdown.
  auto state = connection_->state();
  if (state != Network::Connection::State::Closed) {
    connection_->removeConnectionCallbacks(*this);
    ENVOY_LOG(debug, "Connection callbacks removed");
  }

  // Close the connection if it's still open.
  state = connection_->state();
  if (state == Network::Connection::State::Open) {
    ENVOY_LOG(debug, "Closing open connection gracefully");
    connection_->close(Network::ConnectionCloseType::FlushWrite);
  } else if (state == Network::Connection::State::Closing) {
    ENVOY_LOG(debug, "Connection already closing");
  } else {
    ENVOY_LOG(debug, "Connection already closed");
  }

  // Clear the connection pointer to prevent further access.
  connection_.reset();
  ENVOY_LOG(debug, "RCConnectionWrapper: Shutdown completed");
}

ReverseConnectionIOHandle::ReverseConnectionIOHandle(os_fd_t fd,
                                                     const ReverseConnectionSocketConfig& config,
                                                     Upstream::ClusterManager& cluster_manager,
                                                     ReverseTunnelInitiatorExtension* extension,
                                                     Stats::Scope&)
    : IoSocketHandleImpl(fd), config_(config), cluster_manager_(cluster_manager),
      extension_(extension), original_socket_fd_(fd) {
  ENVOY_LOG(
      debug,
      "Created ReverseConnectionIOHandle: fd={}, src_node={}, src_cluster: {}, num_clusters={}",
      fd_, config_.src_node_id, config_.src_cluster_id, config_.remote_clusters.size());
}

ReverseConnectionIOHandle::~ReverseConnectionIOHandle() {
  ENVOY_LOG(info, "Destroying ReverseConnectionIOHandle - performing cleanup.");
  cleanup();
}

void ReverseConnectionIOHandle::cleanup() {
  ENVOY_LOG(debug, "Starting cleanup of reverse connection resources.");

  // Clean up pipe trigger mechanism first to prevent use-after-free.
  ENVOY_LOG(trace,
            "ReverseConnectionIOHandle: cleaning up trigger pipe; "
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
  if (rev_conn_retry_timer_) {
    ENVOY_LOG(trace, "ReverseConnectionIOHandle: cancelling and resetting retry timer.");
    rev_conn_retry_timer_.reset();
  }
  // Graceful shutdown of connection wrappers with exception safety.
  ENVOY_LOG(debug, "Gracefully shutting down {} connection wrappers.", connection_wrappers_.size());

  // Signal all connections to close gracefully.
  std::vector<std::unique_ptr<RCConnectionWrapper>> wrappers_to_delete;
  for (auto& wrapper : connection_wrappers_) {
    if (wrapper) {
      ENVOY_LOG(debug, "Initiating graceful shutdown for connection wrapper.");
      wrapper->shutdown();
      // Move wrapper for deferred cleanup
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
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Cleaning up {} established connections.",
            queue_size);

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
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Completed established connections cleanup.");

  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Completed cleanup of reverse connection resources.");
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
    ENVOY_LOG(debug, "ReverseConnectionIOHandle: Skipping initializeFileEvent() call because "
                     "reverse connections are already started");
    return;
  }

  ENVOY_LOG(info, "ReverseConnectionIOHandle: Starting reverse connections on worker thread '{}'",
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

  // CRITICAL: Replace the monitored FD with pipe read FD
  // This must happen before any event registration.
  int trigger_fd = getPipeMonitorFd();
  if (trigger_fd != -1) {
    ENVOY_LOG(info, "Replacing monitored FD from {} to pipe read FD {}", fd_, trigger_fd);
    fd_ = trigger_fd;
  }

  // Initialize reverse connections on worker thread
  if (!rev_conn_retry_timer_) {
    rev_conn_retry_timer_ = dispatcher.createTimer([this]() {
      ENVOY_LOG(debug, "Reverse connection timer triggered on worker thread");
      maintainReverseConnections();
    });
    maintainReverseConnections();
  }

  is_reverse_conn_started_ = true;
  ENVOY_LOG(info, "ReverseConnectionIOHandle: Reverse connections started on thread '{}'",
            dispatcher.name());

  // Call parent implementation
  IoSocketHandleImpl::initializeFileEvent(dispatcher, cb, trigger, events);
}

Envoy::Network::IoHandlePtr ReverseConnectionIOHandle::accept(struct sockaddr* addr,
                                                              socklen_t* addrlen) {

  if (isTriggerPipeReady()) {
    char trigger_byte;
    ssize_t bytes_read = ::read(trigger_pipe_read_fd_, &trigger_byte, 1);
    if (bytes_read == 1) {
      ENVOY_LOG(debug, "ReverseConnectionIOHandle: received trigger, processing connection.");
      // When a connection is established, a byte is written to the trigger_pipe_write_fd_ and the
      // connection is inserted into the established_connections_ queue. The last connection in the
      // queue is therefore the one that got established last.
      if (!established_connections_.empty()) {
        ENVOY_LOG(debug, "ReverseConnectionIOHandle: getting connection from queue.");
        auto connection = std::move(established_connections_.front());
        established_connections_.pop();
        // Fill in address information for the reverse tunnel "client".
        // Use actual client address from established connection.
        if (addr && addrlen) {
          const auto& remote_addr = connection->connectionInfoProvider().remoteAddress();

          if (remote_addr) {
            ENVOY_LOG(debug, "ReverseConnectionIOHandle: using actual client address: {}",
                      remote_addr->asString());
            const sockaddr* sock_addr = remote_addr->sockAddr();
            socklen_t addr_len = remote_addr->sockAddrLen();

            if (*addrlen >= addr_len) {
              memcpy(addr, sock_addr, addr_len); // NOLINT(safe-memcpy)
              *addrlen = addr_len;
              ENVOY_LOG(trace, "ReverseConnectionIOHandle: copied {} bytes of address data",
                        addr_len);
            } else {
              ENVOY_LOG(warn,
                        "ReverseConnectionIOHandle::accept() - buffer too small for address: "
                        "need {} bytes, have {}",
                        addr_len, *addrlen);
              *addrlen = addr_len; // Still set the required length
            }
          } else {
            ENVOY_LOG(warn, "ReverseConnectionIOHandle: no remote address available, "
                            "using synthetic localhost address");
            // Fallback to synthetic address only when remote address is unavailable
            auto synthetic_addr =
                std::make_shared<Envoy::Network::Address::Ipv4Instance>("127.0.0.1", 0);
            const sockaddr* sock_addr = synthetic_addr->sockAddr();
            socklen_t addr_len = synthetic_addr->sockAddrLen();
            if (*addrlen >= addr_len) {
              memcpy(addr, sock_addr, addr_len); // NOLINT(safe-memcpy)
              *addrlen = addr_len;
            } else {
              ENVOY_LOG(error, "ReverseConnectionIOHandle: buffer too small for synthetic address");
              *addrlen = addr_len;
            }
          }
        }

        const std::string connection_key =
            connection->connectionInfoProvider().localAddress()->asString();
        ENVOY_LOG(debug, "ReverseConnectionIOHandle: got connection key: {}", connection_key);

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
        ENVOY_LOG(debug,
                  "ReverseConnectionIOHandle: duplicated fd: original_fd={}, duplicated_fd={}",
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
        ENVOY_LOG(debug,
                  "ReverseConnectionIOHandle: RAII IoHandle created with duplicated socket.");
        connection->setSocketReused(true);
        // Close the original connection
        connection->close(Network::ConnectionCloseType::NoFlush);

        ENVOY_LOG(debug, "ReverseConnectionIOHandle: returning io_handle.");
        return io_handle;
      }
    } else if (bytes_read == 0) {
      ENVOY_LOG(debug, "ReverseConnectionIOHandle: trigger pipe closed.");
      return nullptr;
    } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
      ENVOY_LOG(error, "ReverseConnectionIOHandle: error reading from trigger pipe: {}",
                errorDetails(errno));
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
  ENVOY_LOG(error, "ReverseConnectionIOHandle: performing graceful shutdown.");

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

  return IoSocketHandleImpl::close();
}

void ReverseConnectionIOHandle::onEvent(Network::ConnectionEvent event) {
  // This is called when connection events occur.
  // For reverse connections, we handle these events through RCConnectionWrapper.
  ENVOY_LOG(trace, "ReverseConnectionIOHandle: event: {}", static_cast<int>(event));
}

int ReverseConnectionIOHandle::getPipeMonitorFd() const { return trigger_pipe_read_fd_; }

// Use the thread-local registry to get the dispatcher.
Event::Dispatcher& ReverseConnectionIOHandle::getThreadLocalDispatcher() const {
  // Get the thread-local dispatcher from the socket interface's registry.
  auto* local_registry = extension_->getLocalRegistry();

  if (local_registry) {
    // Return the dispatcher from the thread-local registry.
    ENVOY_LOG(debug, "ReverseConnectionIOHandle: dispatcher: {}",
              local_registry->dispatcher().name());
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

    // Update or create host info
    auto host_it = host_to_conn_info_map_.find(host);
    if (host_it == host_to_conn_info_map_.end()) {
      ENVOY_LOG(error, "HostConnectionInfo not found for host {}", host);
    } else {
      // Update cluster name if host moved to different cluster.
      host_it->second.cluster_name = cluster_id;
    }
  }
  cluster_to_resolved_hosts_map_[cluster_id] = new_hosts;
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Removing {} remote hosts from cluster {}",
            removed_hosts.size(), cluster_id);

  // Remove the hosts present in removed_hosts.
  for (const std::string& host : removed_hosts) {
    removeStaleHostAndCloseConnections(host);
    host_to_conn_info_map_.erase(host);
  }
}

void ReverseConnectionIOHandle::removeStaleHostAndCloseConnections(const std::string& host) {
  ENVOY_LOG(info, "ReverseConnectionIOHandle: Removing all connections to remote host {}", host);
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
            "ReverseConnectionIOHandle: Maintaining connections for cluster: {} with {} requested "
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
    resolved_hosts.emplace_back(host_itr.first);
  }
  maybeUpdateHostsMappingsAndConnections(cluster_name, std::move(resolved_hosts));
  // Track successful connections for this cluster.
  uint32_t total_successful_connections = 0;
  uint32_t total_required_connections =
      host_map_ptr->size() * cluster_config.reverse_connection_count;

  // Create connections to each host in the cluster.
  for (const auto& [host_address, host] : *host_map_ptr) {
    ENVOY_LOG(
        debug,
        "ReverseConnectionIOHandle: Checking reverse connection count for host {} of cluster {}",
        host_address, cluster_name);

    // Ensure HostConnectionInfo exists for this host.
    auto host_it = host_to_conn_info_map_.find(host_address);
    if (host_it == host_to_conn_info_map_.end()) {
      ENVOY_LOG(debug, "Creating HostConnectionInfo for host {} in cluster {}", host_address,
                cluster_name);
      host_to_conn_info_map_[host_address] = HostConnectionInfo{
          host_address,
          cluster_name,
          {},                                      // connection_keys - empty set initially
          cluster_config.reverse_connection_count, // target_connection_count from config
          0,                                       // failure_count
          // last_failure_time
          std::chrono::steady_clock::now(), // NO_CHECK_FORMAT(real_time)
          // backoff_until
          std::chrono::steady_clock::now(), // NO_CHECK_FORMAT(real_time)
          {}                                // connection_states
      };
    }

    // Check if we should attempt connection to this host (backoff logic).
    if (!shouldAttemptConnectionToHost(host_address, cluster_name)) {
      ENVOY_LOG(debug,
                "ReverseConnectionIOHandle: Skipping connection attempt to host {} due to backoff",
                host_address);
      continue;
    }
    // Get current number of successful connections to this host.
    uint32_t current_connections = host_to_conn_info_map_[host_address].connection_keys.size();

    ENVOY_LOG(info,
              "ReverseConnectionIOHandle: Number of reverse connections to host {} of cluster {}: "
              "Current: {}, Required: {}",
              host_address, cluster_name, current_connections,
              cluster_config.reverse_connection_count);
    if (current_connections >= cluster_config.reverse_connection_count) {
      ENVOY_LOG(
          debug,
          "ReverseConnectionIOHandle: No more reverse connections needed to host {} of cluster {}",
          host_address, cluster_name);
      total_successful_connections += current_connections;
      continue;
    }
    const uint32_t needed_connections =
        cluster_config.reverse_connection_count - current_connections;

    ENVOY_LOG(debug,
              "ReverseConnectionIOHandle: Initiating {} reverse connections to host {} of remote "
              "cluster '{}' from source node '{}'",
              needed_connections, host_address, cluster_name, config_.src_node_id);
    // Create the required number of connections to this specific host.
    for (uint32_t i = 0; i < needed_connections; ++i) {
      ENVOY_LOG(debug, "Initiating reverse connection number {} to host {} of cluster {}", i + 1,
                host_address, cluster_name);

      bool success = initiateOneReverseConnection(cluster_name, host_address, host);

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
              "ReverseConnectionIOHandle: Successfully created {}/{} total reverse connections to "
              "cluster {}",
              total_successful_connections, total_required_connections, cluster_name);
  } else {
    ENVOY_LOG(error,
              "ReverseConnectionIOHandle: Failed to create any reverse connections to cluster {} - "
              "will retry later",
              cluster_name);
  }
}

bool ReverseConnectionIOHandle::shouldAttemptConnectionToHost(const std::string& host_address,
                                                              const std::string&) {
  if (!config_.enable_circuit_breaker) {
    return true;
  }
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it == host_to_conn_info_map_.end()) {
    // Host entry should be present.
    ENVOY_LOG(error, "HostConnectionInfo not found for host {}", host_address);
    return true;
  }
  auto& host_info = host_it->second;
  auto now = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
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
    ENVOY_LOG(debug, "ReverseConnectionIOHandle: Host {} still in backoff for {}ms", host_address,
              remaining_ms);
    return false;
  }
  return true;
}

void ReverseConnectionIOHandle::trackConnectionFailure(const std::string& host_address,
                                                       const std::string& cluster_name) {
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it == host_to_conn_info_map_.end()) {
    // If the host has been removed from the cluster, we don't need to track the failure.
    ENVOY_LOG(error, "HostConnectionInfo not found for host {}", host_address);
    return;
  }
  auto& host_info = host_it->second;
  host_info.failure_count++;
  host_info.last_failure_time = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
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
  ENVOY_LOG(
      debug,
      "ReverseConnectionIOHandle: Marked host {} in cluster {} as Backoff with connection key {}",
      host_address, cluster_name, backoff_connection_key);
}

void ReverseConnectionIOHandle::resetHostBackoff(const std::string& host_address) {
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it == host_to_conn_info_map_.end()) {
    ENVOY_LOG(error, "HostConnectionInfo not found for host {} - this should not happen",
              host_address);
    return;
  }

  auto& host_info = host_it->second;
  auto now = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)

  // Check if the host is actually in backoff before resetting.
  if (now >= host_info.backoff_until) {
    ENVOY_LOG(debug, "Host {} is not in backoff, skipping reset", host_address);
    return;
  }

  host_info.failure_count = 0;
  host_info.backoff_until = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Reset backoff for host {}", host_address);

  // Mark host as recovered using the same key used by backoff to change the state from backoff to
  // recovered.
  const std::string recovered_connection_key =
      host_address + "_" + host_info.cluster_name + "_backoff";
  updateConnectionState(host_address, host_info.cluster_name, recovered_connection_key,
                        ReverseConnectionState::Recovered);
  ENVOY_LOG(
      debug,
      "ReverseConnectionIOHandle: Marked host {} in cluster {} as Recovered with connection key {}",
      host_address, host_info.cluster_name, recovered_connection_key);
}

void ReverseConnectionIOHandle::updateConnectionState(const std::string& host_address,
                                                      const std::string& cluster_name,
                                                      const std::string& connection_key,
                                                      ReverseConnectionState new_state) {
  // Update connection state in host info and handle old state.
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it != host_to_conn_info_map_.end()) {
    // Remove old state if it exists
    auto old_state_it = host_it->second.connection_states.find(connection_key);
    if (old_state_it != host_it->second.connection_states.end()) {
      ReverseConnectionState old_state = old_state_it->second;
      // Decrement old state gauge using unified function.
      updateStateGauge(host_address, cluster_name, old_state, false /* decrement */);
    }

    // Set new state
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
  // Remove connection state from host info and decrement gauge
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it != host_to_conn_info_map_.end()) {
    auto state_it = host_it->second.connection_states.find(connection_key);
    if (state_it != host_it->second.connection_states.end()) {
      ReverseConnectionState old_state = state_it->second;
      // Decrement state gauge using unified function
      updateStateGauge(host_address, cluster_name, old_state, false /* decrement */);
      // Remove from map
      host_it->second.connection_states.erase(state_it);
    }
  }

  ENVOY_LOG(debug,
            "ReverseConnectionIOHandle: Removed connection {} state for host {} in cluster {}",
            connection_key, host_address, cluster_name);
}

void ReverseConnectionIOHandle::onDownstreamConnectionClosed(const std::string& connection_key) {
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Downstream connection closed: {}", connection_key);

  // Find the host for this connection key
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

  // Remove connection state tracking
  removeConnectionState(host_address, cluster_name, connection_key);

  // The next call to maintainClusterConnections() will detect the missing connection
  // and re-initiate it automatically.
  ENVOY_LOG(debug,
            "ReverseConnectionIOHandle: Connection closure recorded for host {} in cluster {}. "
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
  // Validate required configuration parameters at the top level
  if (config_.src_node_id.empty()) {
    ENVOY_LOG(error, "Source node ID is required but empty - cannot maintain reverse connections");
    return;
  }

  ENVOY_LOG(debug, "Maintaining reverse tunnels for {} clusters.", config_.remote_clusters.size());
  for (const auto& cluster_config : config_.remote_clusters) {
    const std::string& cluster_name = cluster_config.cluster_name;

    ENVOY_LOG(debug, "Processing cluster: {} with {} requested connections per host.", cluster_name,
              cluster_config.reverse_connection_count);
    // Maintain connections for this cluster
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
            "ReverseConnectionIOHandle: Initiating one reverse connection to host {} of cluster "
            "'{}', source node '{}'",
            host_address, cluster_name, config_.src_node_id);
  // Get the thread local cluster
  auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "Cluster '{}' not found", cluster_name);
    updateConnectionState(host_address, cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return false;
  }

  ReverseConnectionLoadBalancerContext lb_context(host_address);

  // Get connection from cluster manager
  Upstream::Host::CreateConnectionData conn_data = thread_local_cluster->tcpConn(&lb_context);

  if (!conn_data.connection_) {
    ENVOY_LOG(error,
              "ReverseConnectionIOHandle: Failed to create connection to host {} in cluster {}",
              host_address, cluster_name);
    updateConnectionState(host_address, cluster_name, temp_connection_key,
                          ReverseConnectionState::CannotConnect);
    return false;
  }

  // Create wrapper to manage the connection
  // The wrapper will initiate and manage the reverse connection handshake using HTTP.
  auto wrapper = std::make_unique<RCConnectionWrapper>(*this, std::move(conn_data.connection_),
                                                       conn_data.host_description_, cluster_name);

  // Send the reverse connection handshake over the TCP connection.
  const std::string connection_key =
      wrapper->connect(config_.src_tenant_id, config_.src_cluster_id, config_.src_node_id);
  ENVOY_LOG(
      debug,
      "ReverseConnectionIOHandle: Initiated reverse connection handshake for host {} with key {}",
      host_address, connection_key);

  // Mark as Connecting after handshake is initiated. Use the actual connection key so that it can
  // be marked as failed in onConnectionDone().
  conn_wrapper_to_host_map_[wrapper.get()] = host_address;
  connection_wrappers_.push_back(std::move(wrapper));

  ENVOY_LOG(debug,
            "ReverseConnectionIOHandle: Successfully initiated reverse connection to host {} "
            "({}:{}) in cluster {}",
            host_address, host->address()->ip()->addressAsString(), host->address()->ip()->port(),
            cluster_name);
  // Reset backoff for successful connection.
  resetHostBackoff(host_address);
  updateConnectionState(host_address, cluster_name, connection_key,
                        ReverseConnectionState::Connecting);
  return true;
}

// Trigger pipe used to wake up accept() when a connection is established.
void ReverseConnectionIOHandle::createTriggerPipe() {
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Creating trigger pipe for single-byte mechanism");
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
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Created trigger pipe: read_fd={}, write_fd={}",
            trigger_pipe_read_fd_, trigger_pipe_write_fd_);
}

bool ReverseConnectionIOHandle::isTriggerPipeReady() const {
  return trigger_pipe_read_fd_ != -1 && trigger_pipe_write_fd_ != -1;
}

void ReverseConnectionIOHandle::onConnectionDone(const std::string& error,
                                                 RCConnectionWrapper* wrapper, bool closed) {
  ENVOY_LOG(debug, "ReverseConnectionIOHandle: Connection wrapper done - error: '{}', closed: {}",
            error, closed);

  // Validate wrapper pointer before any access.
  if (!wrapper) {
    ENVOY_LOG(error, "ReverseConnectionIOHandle: Null wrapper pointer in onConnectionDone");
    return;
  }

  std::string host_address;
  std::string cluster_name;
  std::string connection_key;

  // Safely get host address for wrapper.
  auto wrapper_it = conn_wrapper_to_host_map_.find(wrapper);
  if (wrapper_it == conn_wrapper_to_host_map_.end()) {
    ENVOY_LOG(error, "ReverseConnectionIOHandle: Wrapper not found in conn_wrapper_to_host_map_ - "
                     "may have been cleaned up");
    return;
  }
  host_address = wrapper_it->second;

  // Safely get cluster name from host info.
  auto host_it = host_to_conn_info_map_.find(host_address);
  if (host_it != host_to_conn_info_map_.end()) {
    cluster_name = host_it->second.cluster_name;
  } else {
    ENVOY_LOG(warn, "ReverseConnectionIOHandle: Host info not found for {}, using fallback",
              host_address);
  }

  if (cluster_name.empty()) {
    ENVOY_LOG(error,
              "ReverseConnectionIOHandle: No cluster mapping for host {}, cannot process "
              "connection event",
              host_address);
    // Still try to clean up the wrapper
    conn_wrapper_to_host_map_.erase(wrapper);
    return;
  }

  // Safely get connection info if wrapper is still valid.
  auto* connection = wrapper->getConnection();
  if (connection) {
    connection_key = connection->connectionInfoProvider().localAddress()->asString();
    ENVOY_LOG(debug,
              "ReverseConnectionIOHandle: Processing connection event for host '{}', cluster "
              "'{}', key '{}'",
              host_address, cluster_name, connection_key);
  } else {
    connection_key = "cleanup_" + host_address + "_" + std::to_string(rand());
    ENVOY_LOG(debug, "ReverseConnectionIOHandle: Connection already null, using fallback key '{}'",
              connection_key);
  }

  // Get connection pointer for safe access in success/failure handling.
  connection = wrapper->getConnection();

  // Process connection result safely.
  bool is_success = (error == "reverse connection accepted" || error == "success" ||
                     error == "handshake successful" || error == "connection established");

  if (closed || (!error.empty() && !is_success)) {
    // Handle connection failure
    ENVOY_LOG(error,
              "ReverseConnectionIOHandle: Connection failed - error '{}', cleaning up host {}",
              error, host_address);

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
    // Handle connection success
    ENVOY_LOG(debug, "ReverseConnectionIOHandle: Connection succeeded for host {}", host_address);

    resetHostBackoff(host_address);
    updateConnectionState(host_address, cluster_name, connection_key,
                          ReverseConnectionState::Connected);

    // Only proceed if connection is still valid.
    if (!connection) {
      ENVOY_LOG(
          error,
          "ReverseConnectionIOHandle: Cannot complete successful handshake - connection is null");
      return;
    }

    ENVOY_LOG(info, "ReverseConnectionIOHandle: Transferring tunnel socket for "
                    "reverse_conn_listener consumption");

    // Reset file events safely.
    if (connection->getSocket()) {
      connection->getSocket()->ioHandle().resetFileEvents();
    }

    // Update host connection tracking safely.
    auto host_it = host_to_conn_info_map_.find(host_address);
    if (host_it != host_to_conn_info_map_.end()) {
      host_it->second.connection_keys.insert(connection_key);
      ENVOY_LOG(debug, "ReverseConnectionIOHandle: Added connection key {} for host {}",
                connection_key, host_address);
    }

    Network::ClientConnectionPtr released_conn = wrapper->releaseConnection();

    if (released_conn) {
      ENVOY_LOG(info, "ReverseConnectionIOHandle: Connection will be consumed by "
                      "reverse_conn_listener for HTTP processing");

      // Move connection to established queue for reverse_conn_listener to consume.
      established_connections_.push(std::move(released_conn));

      // Trigger accept mechanism safely.
      if (isTriggerPipeReady()) {
        char trigger_byte = 1;
        ssize_t bytes_written = ::write(trigger_pipe_write_fd_, &trigger_byte, 1);
        if (bytes_written == 1) {
          ENVOY_LOG(info,
                    "ReverseConnectionIOHandle: Successfully triggered reverse_conn_listener "
                    "accept() for host {}",
                    host_address);
        } else {
          ENVOY_LOG(error, "ReverseConnectionIOHandle: Failed to write trigger byte: {}",
                    errorDetails(errno));
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
    ENVOY_LOG(debug, "ReverseConnectionIOHandle: Deferred delete of connection wrapper");
  }
}

// ReverseTunnelInitiator implementation
ReverseTunnelInitiator::ReverseTunnelInitiator(Server::Configuration::ServerFactoryContext& context)
    : extension_(nullptr), context_(&context) {
  ENVOY_LOG(debug, "Created ReverseTunnelInitiator.");
}

DownstreamSocketThreadLocal* ReverseTunnelInitiator::getLocalRegistry() const {
  if (!extension_ || !extension_->getLocalRegistry()) {
    return nullptr;
  }
  return extension_->getLocalRegistry();
}

// ReverseTunnelInitiatorExtension implementation
void ReverseTunnelInitiatorExtension::onServerInitialized() {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension::onServerInitialized");
}

void ReverseTunnelInitiatorExtension::onWorkerThreadInitialized() {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension: creating thread local slot");

  // Create thread local slot on worker thread initialization.
  tls_slot_ =
      ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(context_.threadLocal());

  // Set up the thread local dispatcher for each worker thread.
  tls_slot_->set([this](Event::Dispatcher& dispatcher) {
    return std::make_shared<DownstreamSocketThreadLocal>(dispatcher, context_.scope());
  });

  ENVOY_LOG(
      debug,
      "ReverseTunnelInitiatorExtension: thread local slot created successfully in worker thread");
}

DownstreamSocketThreadLocal* ReverseTunnelInitiatorExtension::getLocalRegistry() const {
  if (!tls_slot_) {
    ENVOY_LOG(error, "ReverseTunnelInitiatorExtension: no thread local slot");
    return nullptr;
  }

  if (auto opt = tls_slot_->get(); opt.has_value()) {
    return &opt.value().get();
  }

  return nullptr;
}

Envoy::Network::IoHandlePtr
ReverseTunnelInitiator::socket(Envoy::Network::Socket::Type socket_type,
                               Envoy::Network::Address::Type addr_type,
                               Envoy::Network::Address::IpVersion version, bool,
                               const Envoy::Network::SocketCreationOptions&) const {
  ENVOY_LOG(debug, "ReverseTunnelInitiator: type={}, addr_type={}", static_cast<int>(socket_type),
            static_cast<int>(addr_type));

  // This method is called without reverse connection config, so create a regular socket.
  int domain;
  if (addr_type == Envoy::Network::Address::Type::Ip) {
    domain = (version == Envoy::Network::Address::IpVersion::v4) ? AF_INET : AF_INET6;
  } else {
    // For pipe addresses.
    domain = AF_UNIX;
  }
  int sock_type = (socket_type == Envoy::Network::Socket::Type::Stream) ? SOCK_STREAM : SOCK_DGRAM;
  int sock_fd = ::socket(domain, sock_type, 0);
  if (sock_fd == -1) {
    ENVOY_LOG(error, "Failed to create fallback socket: {}", errorDetails(errno));
    return nullptr;
  }
  return std::make_unique<Envoy::Network::IoSocketHandleImpl>(sock_fd);
}

/**
 * Thread-safe helper method to create reverse connection socket with config.
 */
Envoy::Network::IoHandlePtr ReverseTunnelInitiator::createReverseConnectionSocket(
    Envoy::Network::Socket::Type socket_type, Envoy::Network::Address::Type addr_type,
    Envoy::Network::Address::IpVersion version, const ReverseConnectionSocketConfig& config) const {

  // Return early if no remote clusters are configured
  if (config.remote_clusters.empty()) {
    ENVOY_LOG(debug, "ReverseTunnelInitiator: No remote clusters configured, returning nullptr");
    return nullptr;
  }

  ENVOY_LOG(debug, "ReverseTunnelInitiator: Creating reverse connection socket for cluster: {}",
            config.remote_clusters[0].cluster_name);

  // For stream sockets on IP addresses, create our reverse connection IOHandle.
  if (socket_type == Envoy::Network::Socket::Type::Stream &&
      addr_type == Envoy::Network::Address::Type::Ip) {
    // Create socket file descriptor using system calls.
    int domain = (version == Envoy::Network::Address::IpVersion::v4) ? AF_INET : AF_INET6;
    int sock_fd = ::socket(domain, SOCK_STREAM, 0);
    if (sock_fd == -1) {
      ENVOY_LOG(error, "Failed to create socket: {}", errorDetails(errno));
      return nullptr;
    }

    ENVOY_LOG(
        debug,
        "ReverseTunnelInitiator: Created socket fd={}, wrapping with ReverseConnectionIOHandle",
        sock_fd);

    // Get the scope from thread local registry, fallback to context scope
    Stats::Scope* scope_ptr = &context_->scope();
    auto* tls_registry = getLocalRegistry();
    if (tls_registry) {
      scope_ptr = &tls_registry->scope();
    }

    // Create ReverseConnectionIOHandle with cluster manager from context and scope.
    return std::make_unique<ReverseConnectionIOHandle>(sock_fd, config, context_->clusterManager(),
                                                       extension_, *scope_ptr);
  }

  // Fall back to regular socket for non-stream or non-IP sockets.
  return socket(socket_type, addr_type, version, false, Envoy::Network::SocketCreationOptions{});
}

Envoy::Network::IoHandlePtr
ReverseTunnelInitiator::socket(Envoy::Network::Socket::Type socket_type,
                               const Envoy::Network::Address::InstanceConstSharedPtr addr,
                               const Envoy::Network::SocketCreationOptions& options) const {

  // Extract reverse connection configuration from address.
  const auto* reverse_addr = dynamic_cast<const ReverseConnectionAddress*>(addr.get());
  if (reverse_addr) {
    // Get the reverse connection config from the address.
    ENVOY_LOG(debug, "ReverseTunnelInitiator: reverse_addr: {}", reverse_addr->asString());
    const auto& config = reverse_addr->reverseConnectionConfig();

    // Convert ReverseConnectionAddress::ReverseConnectionConfig to ReverseConnectionSocketConfig.
    ReverseConnectionSocketConfig socket_config;
    socket_config.src_node_id = config.src_node_id;
    socket_config.src_cluster_id = config.src_cluster_id;
    socket_config.src_tenant_id = config.src_tenant_id;

    // Add the remote cluster configuration.
    RemoteClusterConnectionConfig cluster_config(config.remote_cluster, config.connection_count);
    socket_config.remote_clusters.push_back(cluster_config);

    // Pass config directly to helper method.
    return createReverseConnectionSocket(
        socket_type, addr->type(),
        addr->ip() ? addr->ip()->version() : Envoy::Network::Address::IpVersion::v4, socket_config);
  }

  // Delegate to the other socket() method for non-reverse-connection addresses.
  return socket(socket_type, addr->type(),
                addr->ip() ? addr->ip()->version() : Envoy::Network::Address::IpVersion::v4, false,
                options);
}

bool ReverseTunnelInitiator::ipFamilySupported(int domain) {
  return domain == AF_INET || domain == AF_INET6;
}

Server::BootstrapExtensionPtr ReverseTunnelInitiator::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  ENVOY_LOG(debug, "ReverseTunnelInitiator::createBootstrapExtension()");
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
          DownstreamReverseConnectionSocketInterface&>(config, context.messageValidationVisitor());
  context_ = &context;
  // Create the bootstrap extension and store reference to it.
  auto extension = std::make_unique<ReverseTunnelInitiatorExtension>(context, message);
  extension_ = extension.get();
  return extension;
}

ProtobufTypes::MessagePtr ReverseTunnelInitiator::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
          DownstreamReverseConnectionSocketInterface>();
}

// ReverseTunnelInitiatorExtension constructor implementation.
ReverseTunnelInitiatorExtension::ReverseTunnelInitiatorExtension(
    Server::Configuration::ServerFactoryContext& context,
    const envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
        DownstreamReverseConnectionSocketInterface& config)
    : context_(context), config_(config) {
  ENVOY_LOG(debug, "Created ReverseTunnelInitiatorExtension - TLS slot will be created in "
                   "onWorkerThreadInitialized");
}

void ReverseTunnelInitiatorExtension::updateConnectionStats(const std::string& host_address,
                                                            const std::string& cluster_id,
                                                            const std::string& state_suffix,
                                                            bool increment) {
  // Register stats with Envoy's system for automatic cross-thread aggregation.
  auto& stats_store = context_.scope();

  // Create/update host connection stat with state suffix
  if (!host_address.empty() && !state_suffix.empty()) {
    std::string host_stat_name =
        fmt::format("reverse_connections.host.{}.{}", host_address, state_suffix);
    Stats::StatNameManagedStorage host_stat_name_storage(host_stat_name, stats_store.symbolTable());
    auto& host_gauge = stats_store.gaugeFromStatName(host_stat_name_storage.statName(),
                                                     Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      host_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented host stat {} to {}",
                host_stat_name, host_gauge.value());
    } else {
      host_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented host stat {} to {}",
                host_stat_name, host_gauge.value());
    }
  }

  // Create/update cluster connection stat with state suffix.
  if (!cluster_id.empty() && !state_suffix.empty()) {
    std::string cluster_stat_name =
        fmt::format("reverse_connections.cluster.{}.{}", cluster_id, state_suffix);
    Stats::StatNameManagedStorage cluster_stat_name_storage(cluster_stat_name,
                                                            stats_store.symbolTable());
    auto& cluster_gauge = stats_store.gaugeFromStatName(cluster_stat_name_storage.statName(),
                                                        Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      cluster_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    } else {
      cluster_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    }
  }

  // Also update per-worker stats for debugging.
  updatePerWorkerConnectionStats(host_address, cluster_id, state_suffix, increment);
}

void ReverseTunnelInitiatorExtension::updatePerWorkerConnectionStats(
    const std::string& host_address, const std::string& cluster_id, const std::string& state_suffix,
    bool increment) {
  auto& stats_store = context_.scope();

  // Get dispatcher name from the thread local dispatcher.
  std::string dispatcher_name;
  auto* local_registry = getLocalRegistry();
  if (local_registry == nullptr) {
    ENVOY_LOG(error, "ReverseTunnelInitiatorExtension: No local registry found");
    return;
  }
  // Dispatcher name is of the form "worker_x" where x is the worker index
  dispatcher_name = local_registry->dispatcher().name();
  ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: Updating stats for worker {}",
            dispatcher_name);

  // Create/update per-worker host connection stat.
  if (!host_address.empty() && !state_suffix.empty()) {
    std::string worker_host_stat_name = fmt::format("reverse_connections.{}.host.{}.{}",
                                                    dispatcher_name, host_address, state_suffix);
    Stats::StatNameManagedStorage worker_host_stat_name_storage(worker_host_stat_name,
                                                                stats_store.symbolTable());
    auto& worker_host_gauge = stats_store.gaugeFromStatName(
        worker_host_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_host_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented worker host stat {} to {}",
                worker_host_stat_name, worker_host_gauge.value());
    } else {
      worker_host_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented worker host stat {} to {}",
                worker_host_stat_name, worker_host_gauge.value());
    }
  }

  // Create/update per-worker cluster connection stat.
  if (!cluster_id.empty() && !state_suffix.empty()) {
    std::string worker_cluster_stat_name = fmt::format("reverse_connections.{}.cluster.{}.{}",
                                                       dispatcher_name, cluster_id, state_suffix);
    Stats::StatNameManagedStorage worker_cluster_stat_name_storage(worker_cluster_stat_name,
                                                                   stats_store.symbolTable());
    auto& worker_cluster_gauge = stats_store.gaugeFromStatName(
        worker_cluster_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_cluster_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    } else {
      worker_cluster_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    }
  }
}

absl::flat_hash_map<std::string, uint64_t>
ReverseTunnelInitiatorExtension::getCrossWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Iterate through all gauges and filter for cross-worker stats only.
  // Cross-worker stats have the pattern "reverse_connections.host.<host_address>.<state_suffix>" or
  // "reverse_connections.cluster.<cluster_id>.<state_suffix>" (no dispatcher name in the middle).
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find("reverse_connections.") != std::string::npos &&
        (gauge_name.find("reverse_connections.host.") != std::string::npos ||
         gauge_name.find("reverse_connections.cluster.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(
      debug,
      "ReverseTunnelInitiatorExtension: collected {} stats for reverse connections across all "
      "worker threads",
      stats_map.size());

  return stats_map;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ReverseTunnelInitiatorExtension::getConnectionStatsSync(
    std::chrono::milliseconds /* timeout_ms */) {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension: obtaining reverse connection stats");

  // Get all gauges with the reverse_connections prefix.
  auto connection_stats = getCrossWorkerStatMap();

  std::vector<std::string> connected_hosts;
  std::vector<std::string> accepted_connections;

  // Process the stats to extract connection information
  // For initiator, stats format is: reverse_connections.host.<host>.<state_suffix> or
  // reverse_connections.cluster.<cluster>.<state_suffix> We only want hosts/clusters with
  // "connected" state
  for (const auto& [stat_name, count] : connection_stats) {
    if (count > 0) {
      // Parse stat name to extract host/cluster information with state suffix.
      if (stat_name.find("reverse_connections.host.") != std::string::npos &&
          stat_name.find(".connected") != std::string::npos) {
        // Find the position after "reverse_connections.host." and before ".connected".
        size_t start_pos =
            stat_name.find("reverse_connections.host.") + strlen("reverse_connections.host.");
        size_t end_pos = stat_name.find(".connected");
        if (start_pos != std::string::npos && end_pos != std::string::npos && end_pos > start_pos) {
          std::string host_address = stat_name.substr(start_pos, end_pos - start_pos);
          connected_hosts.push_back(host_address);
        }
      } else if (stat_name.find("reverse_connections.cluster.") != std::string::npos &&
                 stat_name.find(".connected") != std::string::npos) {
        // Find the position after "reverse_connections.cluster." and before ".connected".
        size_t start_pos =
            stat_name.find("reverse_connections.cluster.") + strlen("reverse_connections.cluster.");
        size_t end_pos = stat_name.find(".connected");
        if (start_pos != std::string::npos && end_pos != std::string::npos && end_pos > start_pos) {
          std::string cluster_id = stat_name.substr(start_pos, end_pos - start_pos);
          accepted_connections.push_back(cluster_id);
        }
      }
    }
  }

  ENVOY_LOG(debug,
            "ReverseTunnelInitiatorExtension: found {} connected hosts, {} accepted connections",
            connected_hosts.size(), accepted_connections.size());

  return {connected_hosts, accepted_connections};
}

absl::flat_hash_map<std::string, uint64_t> ReverseTunnelInitiatorExtension::getPerWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Get the current dispatcher name
  std::string dispatcher_name = "main_thread"; // Default for main thread
  auto* local_registry = getLocalRegistry();
  if (local_registry) {
    // Dispatcher name is of the form "worker_x" where x is the worker index.
    dispatcher_name = local_registry->dispatcher().name();
  }
  ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: Getting per worker stats map for {}",
            dispatcher_name);

  // Iterate through all gauges and filter for the current dispatcher.
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map, &dispatcher_name](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find("reverse_connections.") != std::string::npos &&
        gauge_name.find(dispatcher_name + ".") != std::string::npos &&
        (gauge_name.find(".host.") != std::string::npos ||
         gauge_name.find(".cluster.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension: collected {} stats for dispatcher '{}'",
            stats_map.size(), dispatcher_name);

  return stats_map;
}

REGISTER_FACTORY(ReverseTunnelInitiator, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
