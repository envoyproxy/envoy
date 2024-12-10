#include "source/extensions/bootstrap/reverse_connection/reverse_connection_handler_impl.h"

#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/random_generator.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

const std::string ReverseConnectionHandlerImpl::ping_message = "RPING";
std::vector<ReverseConnectionHandlerImpl*> ReverseConnectionHandlerImpl::handlers_{};
absl::Mutex ReverseConnectionHandlerImpl::handler_lock{};

ReverseConnectionHandlerImpl::ReverseConnectionHandlerImpl(Event::Dispatcher* dispatcher)
    : dispatcher_(dispatcher) {
  random_generator_ = std::make_unique<Random::RandomGeneratorImpl>();
  ping_timer_ = dispatcher_->createTimer([this]() { pingConnections(); });

  absl::MutexLock lock(&ReverseConnectionHandlerImpl::handler_lock);
  ReverseConnectionHandlerImpl::handlers_.push_back(this);
}

void ReverseConnectionHandlerImpl::tryEnablePingTimer(const std::chrono::seconds& ping_interval) {
  if (ping_interval_ != std::chrono::seconds::zero()) {
    return;
  }
  ENVOY_LOG(debug, "RC Handler: enabling ping timer, ping interval: {}", ping_interval.count());
  ping_interval_ = ping_interval;
  ping_timer_->enableTimer(ping_interval_);
}

// Use node_to_conn_count_map_ to get the no. of connections for a node ID for a
// particular worker thread. Whenever we receive a new connection to accept, this count
// will be used to look for a better thread to rebalance the connection to. If another
// thread has a lesser count for a particular node than the current thread,
// the connection is moved to that thread.
Network::ReverseConnectionHandler&
ReverseConnectionHandlerImpl::pickMinHandler(const std::string& node_id,
                                             const std::string& cluster_id) {
  absl::WriterMutexLock wlock(&ReverseConnectionHandlerImpl::handler_lock);

  // Assume that this thread is the best candidate.
  ReverseConnectionHandlerImpl* min_handler = this;
  const std::string source_worker = this->dispatcher_->name();

  // Contains the value that we assume to be the minimum value so far.
  int min_handler_count = min_handler->node_to_conn_count_map_[node_id];

  // Iterate over ReverseConnectionHandler instances of all threads to check
  // if any of them have a lower number of accepted reverse connections for
  // the node 'node_id'.
  for (ReverseConnectionHandlerImpl* handler : handlers_) {
    int handler_count = handler->node_to_conn_count_map_[node_id];

    if (handler_count < min_handler_count) {
      min_handler = handler;
      min_handler_count = handler_count;
    }
  }

  const std::string dest_worker = min_handler->dispatcher_->name();
  // Increment the reverse connection count of the chosen handler.

  ENVOY_LOG(info,
            "Picking min RCHandler: Rebalancing socket from worker {} to worker {} with min count "
            "{} for node {} cluster {}",
            source_worker, dest_worker, min_handler->node_to_conn_count_map_[node_id], node_id,
            cluster_id);
  min_handler->node_to_conn_count_map_[node_id]++;
  min_handler->cluster_to_conn_count_map_[cluster_id]++;
  ENVOY_LOG(debug, "Incremented counts: node {} cluster {}", node_to_conn_count_map_[node_id],
            cluster_to_conn_count_map_[cluster_id]);
  return *min_handler;
}

// When the number of worker threads running on responder envoy is more
// than that on initiator envoy, there might be situations where not all
// worker threads have reverse connections for a node since not all have
// accepted reverse connections. In such cases, this function is used to
// rebalance requests to a worker that has a non-zero number of sockets.
ReverseConnectionHandler*
ReverseConnectionHandlerImpl::pickTargetHandler(const std::string& node_id,
                                                const std::string& cluster_id) {
  absl::WriterMutexLock wlock(&ReverseConnectionHandlerImpl::handler_lock);

  const std::string source_worker = this->dispatcher_->name();
  // If cluster ID is provided, we need to look for a handler that has
  // reverse connections for the cluster. If not, we look for a handler
  // that has reverse connections for the node.
  ReverseConnectionHandlerImpl* target_handler = nullptr;
  const std::string& key = (!cluster_id.empty()) ? cluster_id : node_id;

  // Iterate over ReverseConnectionHandler instances of all threads to check
  // if any of them have reverse connections for the given node/cluster.
  for (ReverseConnectionHandlerImpl* handler : handlers_) {
    auto& collection = (!cluster_id.empty()) ? handler->cluster_to_conn_count_map_
                                             : handler->node_to_conn_count_map_;
    if (collection[key] > 0) {
      target_handler = handler;
      break;
    }
  }
  const std::string dest_worker = target_handler->dispatcher_->name();
  ENVOY_LOG(debug,
            "Picking target RCHandler: Rebalancing socket from worker {} to worker {} for node {} "
            "cluster {}",
            source_worker, dest_worker, node_id, cluster_id);
  return target_handler;
}

void ReverseConnectionHandlerImpl::post(const std::string& node_id, const std::string& cluster_id,
                                        Network::ConnectionSocketPtr socket,
                                        const bool expects_proxy_protocol,
                                        const std::chrono::seconds& ping_interval) {
  dispatcher_->post([&, node_id, cluster_id, expects_proxy_protocol, ping_interval,
                     socket = std::move(socket)]() -> void {
    this->addConnectionSocket(node_id, cluster_id, socket, expects_proxy_protocol, ping_interval,
                              true /* rebalanced */);
  });
}
void ReverseConnectionHandlerImpl::addConnectionSocket(
    const std::string& node_id, const std::string& cluster_id, Network::ConnectionSocketPtr socket,
    const bool expects_proxy_protocol, const std::chrono::seconds& ping_interval, bool rebalanced) {
  if (!rebalanced) {
    ReverseConnectionHandler& handler = pickMinHandler(node_id, cluster_id);
    if (&handler != this) {
      ENVOY_LOG(debug, "RC handler: Adding reverse connection to a different worker thread");
      handler.post(node_id, cluster_id, socket, expects_proxy_protocol, ping_interval);
      return;
    } else {
      ENVOY_LOG(debug, "RC handler: Adding reverse connection to the same worker thread");
    }
  }
  ENVOY_LOG(debug, "RC Handler: Adding connection socket for node: {} and remote cluster: {}",
            node_id, cluster_id);
  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& connectionKey = socket->connectionInfoProvider().localAddress()->asString();

  RCHandlerStats* node_stats = this->getStatsByNode(node_id);
  node_stats->reverse_conn_cx_total_.inc();
  node_stats->reverse_conn_cx_idle_.inc();
  ENVOY_LOG(debug, "RC Handler: reverse conn count for node:{} idle: {} total:{}", node_id,
            node_stats->reverse_conn_cx_idle_.value(), node_stats->reverse_conn_cx_total_.value());

  // If local envoy is responding to reverse connections, add the socket to
  // accepted_reverse_connections_. Thereafter, initiate ping keepalives on the socket.
  tryEnablePingTimer(ping_interval);
  accepted_reverse_connections_[node_id].push_back(socket);

  if (!cluster_id.empty()) {
    if (node_to_cluster_map_.find(node_id) == node_to_cluster_map_.end()) {
      node_to_cluster_map_[node_id] = cluster_id;
      cluster_to_node_map_[cluster_id].push_back(node_id);
    }
    RCHandlerStats* cluster_stats = this->getStatsByCluster(cluster_id);
    cluster_stats->reverse_conn_cx_total_.inc();
    cluster_stats->reverse_conn_cx_idle_.inc();

  } else {
    ENVOY_LOG(error, "Found a reverse connection with an empty cluster uuid, and node uuid: {}",
              node_id);
  }

  if (expects_proxy_protocol) {
    expect_proxy_protocol_fd_set_.insert(fd);
    ENVOY_LOG(
        debug,
        "Inserting connection with node: {} connection key: {} and fd: {} into proxy protocol set",
        node_id, connectionKey, fd);
  } else {
    if (expect_proxy_protocol_fd_set_.find(fd) != expect_proxy_protocol_fd_set_.end()) {
      expect_proxy_protocol_fd_set_.erase(fd);
      ENVOY_LOG(debug, "Removing stale entry for fd: {} from proxy protocol set", fd);
    }
  }
  // onPingResponse() expects a ping reply on the socket.
  fd_to_event_map_[fd] = dispatcher_->createFileEvent(
      fd,
      [this, socket](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        onPingResponse(socket->ioHandle());
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  fd_to_timer_map_[fd] =
      dispatcher_->createTimer([this, fd]() { markSocketDead(fd, false /* used */); });
  fd_to_node_map_[fd] = node_id;
  ENVOY_LOG(info, "RC Handler: done adding socket to maps with node: {} connection key: {} fd: {}",
            node_id, connectionKey, fd);
}

void ReverseConnectionHandlerImpl::cleanStaleNodeEntry(const std::string& node_id) {
  // Clean the given node-id, if there are no active sockets.
  if (accepted_reverse_connections_.find(node_id) != accepted_reverse_connections_.end() &&
      accepted_reverse_connections_[node_id].size() > 0) {
    ENVOY_LOG(debug, "Found {} active sockets for node: {}",
              accepted_reverse_connections_[node_id].size(), node_id);
    return;
  }
  ENVOY_LOG(debug, "RC Handler: Cleaning stale node entry for node: {}", node_id);

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
        ENVOY_LOG(debug, "Removing stale node {} from cluster {}", node_id, cluster_itr->first);
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

RCHandlerStats* ReverseConnectionHandlerImpl::getStatsByNode(const std::string& node_id) {
  auto iter = rc_handler_node_stats_map_.find(node_id);
  if (iter != rc_handler_node_stats_map_.end()) {
    RCHandlerStats* stats = iter->second.get();
    return stats;
  }
  ENVOY_LOG(debug, "RC Handler: Creating new stats for node: {}", node_id);
  const std::string& final_prefix = "node." + node_id;
  rc_handler_node_stats_map_[node_id] = std::make_unique<RCHandlerStats>(RCHandlerStats{
      ALL_RCHANDLER_STATS(POOL_GAUGE_PREFIX(*reverse_conn_handler_scope_, final_prefix))});
  return rc_handler_node_stats_map_[node_id].get();
}

RCHandlerStats* ReverseConnectionHandlerImpl::getStatsByCluster(const std::string& cluster_id) {
  auto iter = rc_handler_cluster_stats_map_.find(cluster_id);
  if (iter != rc_handler_cluster_stats_map_.end()) {
    RCHandlerStats* stats = iter->second.get();
    return stats;
  }
  ENVOY_LOG(debug, "RC Handler: Creating new stats for cluster: {}", cluster_id);
  const std::string& final_prefix = "cluster." + cluster_id;
  rc_handler_cluster_stats_map_[cluster_id] = std::make_unique<RCHandlerStats>(RCHandlerStats{
      ALL_RCHANDLER_STATS(POOL_GAUGE_PREFIX(*reverse_conn_handler_scope_, final_prefix))});
  return rc_handler_cluster_stats_map_[cluster_id].get();
}

bool ReverseConnectionHandlerImpl::deleteStatsByNode(const std::string& node_id) {
  const auto& iter = rc_handler_node_stats_map_.find(node_id);
  if (iter == rc_handler_node_stats_map_.end()) {
    return false;
  }
  rc_handler_node_stats_map_.erase(iter);
  return true;
}

bool ReverseConnectionHandlerImpl::deleteStatsByCluster(const std::string& cluster_id) {
  const auto& iter = rc_handler_cluster_stats_map_.find(cluster_id);
  if (iter == rc_handler_cluster_stats_map_.end()) {
    return false;
  }
  rc_handler_cluster_stats_map_.erase(iter);
  return true;
}

void ReverseConnectionHandlerImpl::rebalanceGetConnectionSocket(
    const std::string& key, bool rebalanced,
    std::shared_ptr<std::promise<RCSocketPair>> socket_promise) {
  dispatcher_->post([&, key, rebalanced, socket_promise]() -> void {
    RCSocketPair socket_pair_value = this->getConnectionSocket(key, rebalanced);
    socket_promise->set_value(socket_pair_value);
  });
}

std::pair<Network::ConnectionSocketPtr, bool>
ReverseConnectionHandlerImpl::getConnectionSocket(const std::string& key, bool rebalanced) {
  ENVOY_LOG(debug, "RC Handler: Finding the connection count for cluster/node: {}", key);
  ENVOY_LOG(debug, "RC Handler: Checking whether key: {} is a node or cluster ID.", key);

  // The key can be cluster_id or node_id. If a request has `x-remote-node-id` header
  // set, then the `key` holds `node-id`. Otherwise key contains `cluster-id` value.
  std::string node_id = key;
  std::string cluster_id = "";
  bool should_rebalance = false;

  // If any worker thread has accepted a connection with the key as cluster ID, we treat it
  // as a cluster. Otherwise treat it as a node ID.
  if (getNumberOfSocketsByCluster(key) > 0) {
    cluster_id = key;
    auto cluster_itr = cluster_to_node_map_.find(cluster_id);
    // If this RC Handler has a cluster -> node mapping, pick a random node for the
    // cluster. Otherwise rebalance the request to another Handler that has a connection
    // for the cluster ID.
    if (cluster_itr != cluster_to_node_map_.end()) {
      auto node_idx = random_generator_->random() % cluster_itr->second.size();
      node_id = cluster_itr->second[node_idx];
    } else {
      should_rebalance = true;
    }
  }

  ENVOY_LOG(debug, "RC Handler: node {} cluster {}. Finding connection socket.", node_id,
            cluster_id);

  auto itr = accepted_reverse_connections_.find(node_id);
  if (itr == accepted_reverse_connections_.end() ||
      accepted_reverse_connections_[node_id].size() == 0
      /* this worker has no sockets for the node */) {

    if (rebalanced) {
      return std::make_pair(Network::ConnectionSocketPtr(), false);
    }

    // Evaluate if the request can be serviced on another worker, since the current worker
    // has no sockets for the node.
    if ((!cluster_id.empty() /* request has cluster ID */ && should_rebalance
         /* another worker has sockets for the cluster */) ||
        (cluster_id.empty() /* request has node ID */ &&
         (getNumberOfSocketsByNode(node_id) > 0) /* another worker has sockets for the node */)) {
      ENVOY_LOG(debug, "RC Handler: Request cannot be serviced on this worker. Finding another "
                       "worker to send request to.");
      // Find a worker thread which has accepted reverse connections for node node_id or cluster
      // cluster_id.
      ReverseConnectionHandler* handler = pickTargetHandler(node_id, cluster_id);
      if (handler == nullptr) {
        ENVOY_LOG(error,
                  "RC Handler: cannot find rc handler to rebalance request. node: {} cluster: {}",
                  node_id, cluster_id);
        return std::make_pair(Network::ConnectionSocketPtr(), false);
      }
      auto socket_promise = std::make_shared<std::promise<RCSocketPair>>();
      handler->rebalanceGetConnectionSocket(key, true, socket_promise);
      std::future<RCSocketPair> socket_future = socket_promise->get_future();
      std::future_status status = socket_future.wait_for(std::chrono::milliseconds(1));
      if (status == std::future_status::ready) {
        return socket_future.get();
      } else {
        ENVOY_LOG(error, "RC Handler: rebalancing failed. node: {} cluster: {}", node_id,
                  cluster_id);
        return std::make_pair(Network::ConnectionSocketPtr(), false);
      }
    } else {
      // The request can be serviced on neither this worker, nor any other worker.
      ENVOY_LOG(error, "RC Handler: No sockets found for node: {} cluster: {} on any worker",
                node_id, cluster_id);
      return std::make_pair(Network::ConnectionSocketPtr(), false);
    }
  }
  Network::ConnectionSocketPtr socket(accepted_reverse_connections_[node_id].front());
  accepted_reverse_connections_[node_id].pop_front();

  const int fd = socket->ioHandle().fdDoNotUse();
  const std::string& remoteConnectionKey =
      socket->connectionInfoProvider().remoteAddress()->asString();
  ENVOY_LOG(debug,
            "RC Handler: Reverse conn socket with FD:{} connection key:{} found for node: {} and "
            "cluster: {}",
            fd, remoteConnectionKey, node_id, cluster_id);
  fd_to_event_map_.erase(fd);
  fd_to_timer_map_.erase(fd);

  cleanStaleNodeEntry(node_id);

  RCHandlerStats* node_stats = this->getStatsByNode(node_id);
  node_stats->reverse_conn_cx_idle_.dec();
  node_stats->reverse_conn_cx_used_.inc();

  if (!cluster_id.empty()) {
    RCHandlerStats* cluster_stats = this->getStatsByCluster(cluster_id);
    cluster_stats->reverse_conn_cx_idle_.dec();
    cluster_stats->reverse_conn_cx_used_.inc();
  }

  const bool expects_proxy_protocol =
      expect_proxy_protocol_fd_set_.find(fd) != expect_proxy_protocol_fd_set_.cend();

  if (expects_proxy_protocol) {
    expect_proxy_protocol_fd_set_.erase(fd);
    ENVOY_LOG(debug, "RC Handler: Proxy protocol header expected for connection key: {} FD: {}",
              remoteConnectionKey, fd);
  }

  if (rebalanced) {
    handler_lock.WriterLock();
    this->node_to_conn_count_map_[node_id]--;
    handler_lock.WriterUnlock();
  }
  return std::make_pair(socket, expects_proxy_protocol);
}

uint64_t ReverseConnectionHandlerImpl::getNumberOfSocketsByNode(const std::string& node_id) {
  RCHandlerStats* stats = this->getStatsByNode(node_id);
  ENVOY_LOG(debug, "RC Handler: Number of sockets for node: {} is {}", node_id,
            stats->reverse_conn_cx_idle_.value());
  return stats->reverse_conn_cx_idle_.value();
}

uint64_t ReverseConnectionHandlerImpl::getNumberOfSocketsByCluster(const std::string& cluster_id) {
  RCHandlerStats* stats = this->getStatsByCluster(cluster_id);
  ENVOY_LOG(debug, "RC Handler: Number of sockets for cluster: {} is {}", cluster_id,
            stats->reverse_conn_cx_idle_.value());
  return stats->reverse_conn_cx_idle_.value();
}

absl::flat_hash_map<std::string, size_t> ReverseConnectionHandlerImpl::getSocketCountMap() {
  absl::flat_hash_map<std::string, size_t> response;
  for (auto& itr : rc_handler_node_stats_map_) {
    response[itr.first] = rc_handler_node_stats_map_[itr.first]->reverse_conn_cx_total_.value();
  }
  return response;
}

void ReverseConnectionHandlerImpl::markSocketDead(const int fd, const bool used) {
  const std::string& node_id = fd_to_node_map_[fd];
  std::string cluster_id = (node_to_cluster_map_.find(node_id) != node_to_cluster_map_.end())
                               ? node_to_cluster_map_[node_id]
                               : "";
  fd_to_node_map_.erase(fd);

  // If this is a used connection, we update the stats and return.
  if (used) {
    ENVOY_LOG(debug, "RC Handler: Marking used socket dead. node: {} FD: {}", node_id, fd);
    RCHandlerStats* stats = this->getStatsByNode(node_id);
    stats->reverse_conn_cx_used_.dec();
    stats->reverse_conn_cx_total_.dec();
    absl::MutexLock lock(&ReverseConnectionHandlerImpl::handler_lock);
    node_to_conn_count_map_[node_id]--;

    const auto& iter = expect_proxy_protocol_fd_set_.find(fd);
    RELEASE_ASSERT(iter == expect_proxy_protocol_fd_set_.end(), "Unexpected fd found in set");
    return;
  }

  auto& sockets = accepted_reverse_connections_[node_id];
  bool socket_found = false;
  for (auto itr = sockets.begin(); itr != sockets.end(); itr++) {
    if (fd == itr->get()->ioHandle().fdDoNotUse()) {
      ENVOY_LOG(debug, "RC Handler: Marking socket dead; cluster: {} FD: {}", node_id, fd);
      ::shutdown(fd, SHUT_RDWR);
      itr = sockets.erase(itr);
      socket_found = true;

      fd_to_event_map_.erase(fd);
      fd_to_timer_map_.erase(fd);
      if (expect_proxy_protocol_fd_set_.find(fd) != expect_proxy_protocol_fd_set_.end()) {
        expect_proxy_protocol_fd_set_.erase(fd);
      }

      RCHandlerStats* node_stats = this->getStatsByNode(node_id);
      node_stats->reverse_conn_cx_idle_.dec();
      node_stats->reverse_conn_cx_total_.dec();

      if (!cluster_id.empty()) {
        RCHandlerStats* cluster_stats = this->getStatsByCluster(cluster_id);
        cluster_stats->reverse_conn_cx_idle_.dec();
        cluster_stats->reverse_conn_cx_total_.dec();
      }

      handler_lock.WriterLock();
      node_to_conn_count_map_[node_id]--;
      if (!cluster_id.empty()) {
        cluster_to_conn_count_map_[cluster_id]--;
      }
      handler_lock.WriterUnlock();
      break;
    }
  }

  if (!socket_found) {
    ENVOY_LOG(error, "RC Handler: Marking an invalid socket dead. node: {} FD: {}", node_id, fd);
  }

  if (sockets.size() == 0) {
    cleanStaleNodeEntry(node_id);
  }
}

void ReverseConnectionHandlerImpl::onPingResponse(Network::IoHandle& io_handle) {
  const int fd = io_handle.fdDoNotUse();

  Buffer::OwnedImpl buffer;
  Api::IoCallUint64Result result = io_handle.read(buffer, absl::make_optional(ping_message.size()));
  if (!result.ok()) {
    ENVOY_LOG(debug, "RC Handler: Read error on FD: {}: error - {}", fd,
              result.err_->getErrorDetails());
    markSocketDead(fd, false /* used */);
    return;
  }

  // In this case, there is no read error, but the socket has been closed by the remote
  // peer in a graceful manner, unlike a connection refused, or a reset.
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "RC Handler: FD: {}: reverse connection closed", fd);
    markSocketDead(fd, false /* used */);
    return;
  }

  if (result.return_value_ < ping_message.size()) {
    ENVOY_LOG(debug, "RC Handler: FD: {}: no complete ping data yet", fd);
    return;
  }

  if (buffer.toString() != ping_message) {
    ENVOY_LOG(debug, "RC Handler: FD: {}: response is not {}", fd, ping_message);
    markSocketDead(fd, false /* used */);
    return;
  }
  ENVOY_LOG(debug, "RC Handler: FD: {}: received ping response", fd);
  fd_to_timer_map_[fd]->disableTimer();
}

void ReverseConnectionHandlerImpl::pingConnections(const std::string& node_id) {
  ENVOY_LOG(debug, "RC Handler: Pinging connections for cluster: {}", node_id);
  auto& sockets = accepted_reverse_connections_[node_id];
  ENVOY_LOG(debug, "Number of sockets: {}", sockets.size());
  for (auto itr = sockets.begin(); itr != sockets.end(); itr++) {
    int fd = itr->get()->ioHandle().fdDoNotUse();
    Buffer::OwnedImpl buffer(ping_message);

    auto ping_response_timeout = ping_interval_ / 2;
    fd_to_timer_map_[fd]->enableTimer(ping_response_timeout);
    while (buffer.length() > 0) {
      Api::IoCallUint64Result result = itr->get()->ioHandle().write(buffer);
      ENVOY_LOG(trace, "RC Handler: FD: {}: sending ping request. return_value: {}", fd,
                result.return_value_);
      if (result.return_value_ == 0) {
        ENVOY_LOG(debug, "RC Handler: FD: {}: sending ping rc {}, error - ", fd,
                  result.return_value_, result.err_->getErrorDetails());
        if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          ENVOY_LOG(debug, "RC Handler: FD: {}: failed to send ping", fd);
          ::shutdown(fd, SHUT_RDWR);
          sockets.erase(itr--);
          cleanStaleNodeEntry(node_id);
          break;
        }
      }
    }

    if (buffer.length() > 0) {
      continue;
    }
  }
}

void ReverseConnectionHandlerImpl::pingConnections() {
  for (auto& itr : accepted_reverse_connections_) {
    pingConnections(itr.first);
  }
  ping_timer_->enableTimer(ping_interval_);
}

void ReverseConnectionHandlerImpl::initializeStats(Stats::Scope& scope) {
  const std::string stats_prefix = "reverse_connection_handler.";
  reverse_conn_handler_scope_ = scope.createScope(stats_prefix);
  ENVOY_LOG(debug, "Initialized RCHandler stats; scope: {}",
            reverse_conn_handler_scope_->constSymbolTable().toString(
                reverse_conn_handler_scope_->prefix()));
}

absl::flat_hash_map<std::string, size_t> ReverseConnectionHandlerImpl::getConnectionStats() {

  absl::flat_hash_map<std::string, size_t> response;
  for (auto& itr : accepted_reverse_connections_) {
    ENVOY_LOG(debug, "reverse_connection handler: found {} accepted connections for {}",
              itr.second.size(), itr.first);
    response[itr.first] = itr.second.size();
  }
  return response;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
