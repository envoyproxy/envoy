#include "source/extensions/bootstrap/reverse_connection/reverse_connection_initiator.h"

#include "envoy/network/listener.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

const std::string ReverseConnectionInitiator::reverse_connections_path = "/reverse_connections";
const std::string ReverseConnectionInitiator::reverse_connections_request_path =
    "/reverse_connections/request";
const std::chrono::milliseconds rev_conn_retry_timeout = std::chrono::milliseconds(10000);

ReverseConnectionInitiator::ReverseConnectionInitiator(const Network::ListenerConfig& listener_ref,
                                                       const ReverseConnectionOptions& options,
                                                       ReverseConnectionManagerImpl& rc_manager,
                                                       Stats::Scope& scope)
    : listener_ref_(listener_ref), rc_options_(std::move(options)), parent_rc_manager_(rc_manager) {
  ENVOY_LOG(trace, "ReverseConnectionInitiator constructor called for listener: {} dispatcher: {}",
            listener_ref.name(), parent_rc_manager_.dispatcher().name());
  ASSERT(parent_rc_manager_.dispatcher().isThreadSafe());
  rev_conn_retry_timer_ =
      parent_rc_manager_.dispatcher().createTimer([this]() -> void { maintainConnCount(); });
  initializeStats(scope);
}

ReverseConnectionInitiator::~ReverseConnectionInitiator() {
  ENVOY_LOG(debug, "RCInitiator ID: {}; Closing all connections in internal maps", getID());
  // Map of cluster -> number of connections closed, used to update stats.
  absl::flat_hash_map<std::string, int64_t> rc_stat_conn_to_delete_map;
  for (const auto& conn_iter : rc_conn_to_host_map_) {
    ENVOY_LOG(debug, "Unregistering connection: {}", conn_iter.first);
    rc_stat_conn_to_delete_map[conn_iter.second] += 1;
    parent_rc_manager_.unregisterConnection(conn_iter.first);
  }
  ENVOY_LOG(debug, "RCInitiator ID: {}; Closed all connections in internal maps", getID());
}

void ReverseConnectionInitiator::initializeStats(Stats::Scope& scope) {
  const std::string stats_prefix =
      listener_ref_.name() + "." + std::to_string(listener_ref_.listenerTag());
  reverse_conn_scope_ = scope.createScope(stats_prefix);
  ENVOY_LOG(debug, "Initialized RCInitiator stats with scope: {}",
            reverse_conn_scope_->constSymbolTable().toString(reverse_conn_scope_->prefix()));
}

void ReverseConnectionInitiator::addStatshandlerForCluster(const std::string& cluster_name) {
  if (rc_stats_map_.find(cluster_name) == rc_stats_map_.end()) {
    rc_stats_map_[cluster_name] = std::make_unique<RCInitiatorStats>(RCInitiatorStats{
        ALL_RCINITIATOR_STATS(POOL_GAUGE_PREFIX(*reverse_conn_scope_, cluster_name))});
  }
}

void ReverseConnectionInitiator::removeStaleHostAndCloseConnections(const std::string& host) {
  const auto& host_to_rc_conns_itr = host_to_rc_conns_map_.find(host);
  if (host_to_rc_conns_itr != host_to_rc_conns_map_.end()) {
    ENVOY_LOG(info, "RCInitiator ID: {} Removing {} connections to remote host {}", getID(),
              host_to_rc_conns_itr->second.size(), host);

    // Terminate all connections to the specified host and then cleans up its associated data.
    const absl::flat_hash_set<std::string>& conn_set = host_to_rc_conns_itr->second;
    for (const std::string& conn : conn_set) {
      ENVOY_LOG(debug, "RCInitiator ID: {} Removing connection {} to remote host {}", getID(), conn,
                host);
      rc_conn_to_host_map_.erase(conn);
      parent_rc_manager_.unregisterConnection(conn);
    }

    host_to_rc_conns_map_.erase(host);
  }
}

void ReverseConnectionInitiator::maybeUpdateHostsMappingsAndConnections(
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

    ENVOY_LOG(debug, "RCInitiator ID: {} Adding remote host {} to cluster {}", getID(), host,
              cluster_id);
    host_to_cluster_map_[host] = cluster_id;
  }

  cluster_to_resolved_hosts_map_[cluster_id] = new_hosts;

  ENVOY_LOG(debug, "RCInitiator ID: {} Removing {} remote host of cluster {}", getID(),
            removed_hosts.size(), cluster_id);
  // Remove the hosts present in removed_hosts.
  for (const std::string& host : removed_hosts) {
    removeStaleHostAndCloseConnections(host);
    host_to_cluster_map_.erase(host);
  }
}

bool ReverseConnectionInitiator::maintainConnCount() {

  // For all cluster-conn pairs in remote_cluster_to_conns_, initiate the required number of
  // reverse connections.
  bool success = true;
  for (const auto& cluster_conn : rc_options_.remote_cluster_to_conns_) {

    const std::string remote_cluster_id = cluster_conn.first;

    // Create a RCInitiatorStats for every remote cluster, so that stats can be logged in the format
    // "base_scope.cluster.metric".
    addStatshandlerForCluster(remote_cluster_id);

    // Retrieve the resolved hosts for a cluster and update the corresponding maps.
    const auto thread_local_cluster =
        parent_rc_manager_.dispatcher().getClusterManager()->getThreadLocalCluster(
            remote_cluster_id);

    // The thread_local_cluster will be nullptr during any LDS or CDS update.
    // In these cases, we should wait for the next iteration.
    if (!thread_local_cluster) {
      ENVOY_LOG(trace, "RCInitiator ID: {} Cluster {} not found", getID(), remote_cluster_id);
      success = false;
      break;
    }

    const auto& host_map_ptr = thread_local_cluster->prioritySet().crossPriorityHostMap();

    std::vector<std::string> resolved_hosts;
    if (host_map_ptr != nullptr) {
      for (const auto& host_iter : *host_map_ptr) {
        resolved_hosts.emplace_back(host_iter.first);
      }
    }
    maybeUpdateHostsMappingsAndConnections(remote_cluster_id, std::move(resolved_hosts));

    ENVOY_LOG(debug, "RCInitiator ID: {} Checking reverse connection count for cluster {}", getID(),
              remote_cluster_id);

    // Obtain the number of connections already present to each host of a cluster. If less than the
    // requested count, initiate more connections.
    const absl::flat_hash_set<std::string>& hosts =
        cluster_to_resolved_hosts_map_[remote_cluster_id];
    for (const std::string& host : hosts) {
      uint32_t curr_no_of_conns = 0;
      if (host_to_rc_conns_map_.find(host) != host_to_rc_conns_map_.end()) {
        curr_no_of_conns = host_to_rc_conns_map_[host].size();
      }

      const uint32_t req_no_of_conns = cluster_conn.second - curr_no_of_conns;
      ENVOY_LOG(info,
                "RCInitiator ID: {} Number of reverse connections to host {} of cluster {}: "
                "Current: {}: Required: {}",
                getID(), host, remote_cluster_id, curr_no_of_conns, cluster_conn.second);

      if (req_no_of_conns <= 0) {
        ENVOY_LOG(debug, "No more reverse connections needed to host {} of cluster {}", host,
                  remote_cluster_id);
        continue;
      }

      ENVOY_LOG(debug,
                "RCInitiator ID: {} Initiating {} reverse connections to host {} of remote "
                "cluster '{}' from source node '{}'",
                getID(), host, req_no_of_conns, remote_cluster_id, rc_options_.src_node_id_);

      for (uint32_t conn = 0; conn < req_no_of_conns; conn++) {
        ENVOY_LOG(debug, "Initiating reverse connection number {} to host {} of {}", conn + 1, host,
                  remote_cluster_id);
        success = ReverseConnectionInitiator::initiateOneReverseConnection(remote_cluster_id, host);
        if (!success) {
          ENVOY_LOG(error, "Failed to initiate reverse connection number {} to host {} of {}",
                    conn + 1, host, remote_cluster_id);
        } else {
          ENVOY_LOG(debug, "Initiated reverse connection number {} to host {} of {}", conn + 1,
                    host, remote_cluster_id);
        }
      }
      ENVOY_LOG(debug,
                "RCInitiator ID:{} : Initiated {} reverse connections to host {} of remote "
                "cluster '{}' from source node '{}'",
                getID(), req_no_of_conns, host, remote_cluster_id, rc_options_.src_node_id_);
    }
  }
  rev_conn_retry_timer_->enableTimer(rev_conn_retry_timeout);
  return success;
}

bool ReverseConnectionInitiator::initiateOneReverseConnection(const std::string& remote_cluster_id,
                                                              const std::string& host) {
  if (rc_options_.src_node_id_.empty() || remote_cluster_id.empty() || host.empty()) {
    ENVOY_LOG(error,
              "Source node ID, Host and Remote cluster ID are requried; Source node: {} Host: {} "
              "Remote Cluster: {}",
              rc_options_.src_node_id_, host, remote_cluster_id);
    return false;
  }
  ENVOY_LOG(debug,
            "Initiating one reverse connection to host {} of remote cluster '{}', source node '{}'",
            host, remote_cluster_id, rc_options_.src_node_id_);

  // Get the thread local cluster object for the remote_cluster_id, connect to it and send a HTTP
  // request for reverse connection initiation through it.
  Upstream::Host::CreateConnectionData conn_data;
  try {
    const auto thread_local_cluster =
        parent_rc_manager_.dispatcher().getClusterManager()->getThreadLocalCluster(
            remote_cluster_id);
    if (thread_local_cluster != nullptr) {
      ReverseConnectionLoadBalancerContext lb_context(host);
      conn_data = thread_local_cluster->tcpConn(&lb_context);
    } else {
      ENVOY_LOG(error, "failed to create connection - remote cluster {} not found",
                remote_cluster_id);
      return false;
    }
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "failed to create connection - {}", e.what());
    return false;
  }
  if (!conn_data.connection_) {
    ENVOY_LOG(error, "failed to create connection - Connection object not found");
    return false;
  }

  RCConnectionManager* rcManagerPtr =
      new RCConnectionManager(parent_rc_manager_, listener_ref_, std::move(conn_data.connection_));

  const std::string& connectionKey = rcManagerPtr->connect(rc_options_);

  // Store the connection -> remote host mapping.
  rc_conn_to_host_map_[connectionKey] = host;
  ENVOY_LOG(debug, "Added connection {}, host {} and cluster {} to rc_conn_to_host_map_",
            connectionKey, host, remote_cluster_id);
  return true;
}

void ReverseConnectionInitiator::reverseConnectionDone(const std::string& error,
                                                       RCConnectionManagerPtr rc_connection_manager,
                                                       bool connectionClosed) {

  const Network::ConnectionSocketPtr& client_socket =
      rc_connection_manager->getConnection()->getSocket();
  const std::string& connectionKey =
      client_socket->connectionInfoProvider().localAddress()->asString();
  const std::string& remote_cluster_id = getRemoteClusterForConn(connectionKey);

  // Occasionally, connection closure feedback may arrive after the host has been unregistered
  // from the cluster. In such cases, we have already cleaned up the corresponding entry
  // from our maps.
  if (remote_cluster_id.empty()) {
    ENVOY_LOG(error, "Reverse connection failed: Internal Error: socket -> remote cluster mapping "
                     "not present. Ignoring message");
    return;
  }
  ENVOY_LOG(debug,
            "Got response from initiated reverse connection with connection key: {},"
            "cluster '{}', error {}",
            connectionKey, remote_cluster_id, absl::string_view(error));

  if (connectionClosed || error.size() > 0) {
    if (error.size() > 0) {
      std::string msg =
          fmt::format("Reverse connection failed: Received error {} from remote envoy", error);
      ENVOY_LOG(error, msg);
      rc_connection_manager->onFailure();
      rc_connection_manager->getConnection()->close(Network::ConnectionCloseType::NoFlush,
                                                    "reverse_conn_error");
    }

    ENVOY_LOG(error, "Reverse connection failed: Removing connection {} to host {}", connectionKey,
              rc_conn_to_host_map_[connectionKey]);
    rc_conn_to_host_map_.erase(connectionKey);
  } else {
    rc_connection_manager->getConnection()->setConnectionReused(true);
    client_socket->ioHandle().resetFileEvents();

    rc_stats_map_[remote_cluster_id]->reverse_conn_cx_total_.inc();
    rc_stats_map_[remote_cluster_id]->reverse_conn_cx_idle_.inc();

    // After the connection initiation completes successfully, register the connection key to that
    // host.
    const std::string& host = rc_conn_to_host_map_[connectionKey];
    if (host_to_rc_conns_map_.find(host) == host_to_rc_conns_map_.end()) {
      host_to_rc_conns_map_[host] = absl::flat_hash_set<std::string>();
    }

    ENVOY_LOG(debug, "RCInitiator ID: {} Adding connection key: {} to remote host {} of cluster {}",
              getID(), connectionKey, host, remote_cluster_id);
    host_to_rc_conns_map_[host].insert(connectionKey);

    parent_rc_manager_.registerConnection(connectionKey, this);

    // Save the socket as a upstream conn socket to the listener. The socket will henceforth
    // be owned and operated by the listener.
    parent_rc_manager_.dispatcher().connectionHandler()->saveUpstreamConnection(
        const_cast<Network::ConnectionSocketPtr&&>(client_socket), listener_ref_.listenerTag());
    ENVOY_LOG(debug,
              "Passed upstream connection for remote host of remote cluster {} to listener {}",
              host, remote_cluster_id, listener_ref_.name());
  }
}

std::string ReverseConnectionInitiator::getRemoteClusterForConn(const std::string& connectionKey) {
  const auto& rc_conn_to_host_map_itr = rc_conn_to_host_map_.find(connectionKey);
  if (rc_conn_to_host_map_itr == rc_conn_to_host_map_.end()) {
    return std::string();
  }
  return host_to_cluster_map_[rc_conn_to_host_map_itr->second];
}

void ReverseConnectionInitiator::markConnUsed(const std::string& connectionKey) {
  const std::string remote_cluster_id = getRemoteClusterForConn(connectionKey);
  rc_stats_map_[remote_cluster_id]->reverse_conn_cx_used_.inc();
  rc_stats_map_[remote_cluster_id]->reverse_conn_cx_idle_.dec();
}

void ReverseConnectionInitiator::notifyConnectionClose(const std::string& connectionKey,
                                                       const bool is_used) {

  const std::string remote_cluster_id = getRemoteClusterForConn(connectionKey);

  // Occasionally, connection closure feedback may arrive after the host has been unregistered
  // from the cluster. In such cases, we have already cleaned up the corresponding entry
  // from our maps.
  if (remote_cluster_id.empty()) {
    return;
  }

  const std::string& host = rc_conn_to_host_map_[connectionKey];
  if (!host.empty()) {
    host_to_rc_conns_map_[host].erase(connectionKey);
    rc_conn_to_host_map_.erase(connectionKey);
  }

  ENVOY_LOG(debug, "RCInitiator {}: Erased connection {} to host {} from internal data structures",
            this->listener_ref_.listenerTag(), connectionKey, host);

  // Update the stats to log the closed connection.
  rc_stats_map_[remote_cluster_id]->reverse_conn_cx_total_.dec();
  if (is_used) {
    rc_stats_map_[remote_cluster_id]->reverse_conn_cx_used_.dec();
  } else {
    rc_stats_map_[remote_cluster_id]->reverse_conn_cx_idle_.dec();
  }
}

uint64_t ReverseConnectionInitiator::getNumberOfSockets(const std::string& key) {
  return (rc_stats_map_.find(key) != rc_stats_map_.end())
             ? rc_stats_map_[key]->reverse_conn_cx_idle_.value()
             : 0;
}

void ReverseConnectionInitiator::getSocketCountMap(
    absl::flat_hash_map<std::string, size_t>& response) {
  for (auto& itr : rc_stats_map_) {
    ENVOY_LOG(debug, "getSocketCountMap cluster: {} Conn count: {}", itr.first,
              rc_stats_map_[itr.first]->reverse_conn_cx_idle_.value());
    response[itr.first] += rc_stats_map_[itr.first]->reverse_conn_cx_idle_.value();
  }
}

void ReverseConnectionInitiator::RCConnectionManager::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    const std::string& connectionKey =
        connection_->getSocket()->connectionInfoProvider().localAddress()->asString();
    ENVOY_LOG(debug, "RCConnectionManager: connection: {}, found connection {} remote closed",
              connection_->id(), connectionKey);
    onFailure();
    parent_rc_manager_.dispatcher().post([this]() -> void {
      RCConnectionManagerPtr rc_connection_manager = RCConnectionManagerPtr(this);
      ReverseConnectionInitiator* rc_initiator =
          parent_rc_manager_.getRCInitiatorPtr(listener_ref_);
      if (rc_initiator) {
        rc_initiator->reverseConnectionDone(std::string() /* error */,
                                            std::move(rc_connection_manager),
                                            true /* connectionClosed */);
      }
    });
  }
}

std::string ReverseConnectionInitiator::RCConnectionManager::connect(
    const ReverseConnectionOptions& rc_options) {
  // Register connection callbacks to handle cleanup when the connection is closed.
  ENVOY_LOG(debug, "RCConnectionManager: connection: {}, adding connection callbacks",
            connection_->id());
  connection_->addConnectionCallbacks(*this);

  // Add read filter so that the response from the remote envoy is read.
  ENVOY_LOG(debug, "RCConnectionManager: connection: {}, adding read filter", connection_->id());
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new ConnReadFilter(this)});
  connection_->connect();

  ENVOY_LOG(debug,
            "RCConnectionManager: connection: {}, sending reverse connection creation "
            "request through TCP",
            connection_->id());
  // Send http request for RC creation over a plain tcp connection.
  envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeArg arg;
  arg.set_tenant_uuid(rc_options.src_tenant_id_);
  arg.set_cluster_uuid(rc_options.src_cluster_id_);
  arg.set_node_uuid(rc_options.src_node_id_);
  std::string body = arg.SerializeAsString();
  Buffer::OwnedImpl reverse_connection_request(fmt::format(
      "POST {} HTTP/1.1\r\nHost: {}\r\nAccept: "
      "*/*\r\nContent-length: {}\r\n\r\n{}",
      ReverseConnectionInitiator::reverse_connections_request_path,
      connection_->connectionInfoProvider().remoteAddress()->asStringView(), body.length(), body));
  ENVOY_LOG(debug, "RCConnectionManager: connection: {}, writing request to connection: {}",
            connection_->id(), reverse_connection_request.toString());
  connection_->write(reverse_connection_request, false);
  return connection_->getSocket()->connectionInfoProvider().localAddress()->asString();
}

void ReverseConnectionInitiator::RCConnectionManager::onData(const std::string& error) {
  RCConnectionManagerPtr rc_connection_manager = RCConnectionManagerPtr(this);
  ReverseConnectionInitiator* rc_initiator = parent_rc_manager_.getRCInitiatorPtr(listener_ref_);
  if (rc_initiator) {
    rc_initiator->reverseConnectionDone(error, std::move(rc_connection_manager),
                                        false /* connectionClosed */);
  }
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
