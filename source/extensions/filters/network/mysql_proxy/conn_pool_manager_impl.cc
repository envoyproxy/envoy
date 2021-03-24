#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"
#include "extensions/filters/network/mysql_proxy/conn_pool_manager.h"
#include "extensions/filters/network/mysql_proxy/conn_pool_manager_impl.h"
#include "extensions/filters/network/mysql_proxy/new_conn_pool_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnPool {

ConnectionPool::Cancellable*
ConnectionManagerImpl::newConnection(Envoy::Tcp::ConnectionPool::Callbacks& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().newConnection(callbacks);
}

void ConnectionManagerImpl::ThreadLocalPool::onClusterAddOrUpdateNonVirtual(
    Upstream::ThreadLocalCluster& cluster) {
  if (cluster.info()->name() != cluster_name_) {
    return;
  }
  // Ensure the filter is not deleted in the main thread during this method.
  auto shared_parent = parent_.lock();
  if (!shared_parent) {
    return;
  }

  if (cluster_ != nullptr) {
    // Treat an update as a removal followed by an add.
    ThreadLocalPool::onClusterRemoval(cluster_name_);
  }

  ASSERT(cluster_ == nullptr);
  cluster_ = &cluster;
  ASSERT(host_set_member_update_cb_handle_ == nullptr);
  host_set_member_update_cb_handle_ = cluster_->prioritySet().addMemberUpdateCb(
      [this](const std::vector<Upstream::HostSharedPtr>& hosts_added,
             const std::vector<Upstream::HostSharedPtr>& hosts_removed) -> void {
        onHostsAdded(hosts_added);
        onHostsRemoved(hosts_removed);
      });

  ASSERT(host_address_map_.empty());
  for (const auto& i : cluster_->prioritySet().hostSetsPerPriority()) {
    for (auto& host : i->hosts()) {
      host_address_map_[host->address()->asString()] = host;
    }
  }
}

void ConnectionManagerImpl::ThreadLocalPool::onHostsAdded(
    const std::vector<Upstream::HostSharedPtr>& hosts_added) {
  for (const auto& host : hosts_added) {
    std::string host_address = host->address()->asString();
    // Insert new host into address map, possibly overwriting a previous host's entry.
    host_address_map_[host_address] = host;
    for (const auto& created_host : created_via_redirect_hosts_) {
      if (created_host->address()->asString() == host_address) {
        // Remove our "temporary" host created in makeRequestToHost().
        onHostsRemoved({created_host});
        created_via_redirect_hosts_.remove(created_host);
        break;
      }
    }
  }
}

void ConnectionManagerImpl::ThreadLocalPool::onHostsRemoved(
    const std::vector<Upstream::HostSharedPtr>& hosts_removed) {
  for (const auto& host : hosts_removed) {
    auto it = pools_.find(host);
    if (it != pools_.end()) {
      it->second->drainConnections();
      pools_to_drain_.push_back(std::move(it->second));
      pools_.erase(it);
    }
    auto it2 = host_address_map_.find(host->address()->asString());
    if ((it2 != host_address_map_.end()) && (it2->second == host)) {
      host_address_map_.erase(it2);
    }
  }
}

void ConnectionManagerImpl::ThreadLocalPool::onClusterRemoval(const std::string& cluster_name) {
  if (cluster_name != cluster_name_) {
    return;
  }

  // Treat cluster removal as a removal of all hosts. Close all connections and fail all pending
  // requests.
  host_set_member_update_cb_handle_ = nullptr;
  while (!pools_.empty()) {
    pools_.begin()->second->closeConnections();
    pools_.erase(pools_.begin());
  }
  while (!pools_to_drain_.empty()) {
    pools_to_drain_.front()->closeConnections();
    pools_to_drain_.pop_front();
  }
  cluster_ = nullptr;
  host_address_map_.clear();
}

ConnectionPool::Cancellable* ConnectionManagerImpl::ThreadLocalPool::newConnection(
    Envoy::Tcp::ConnectionPool::Callbacks& callbacks) {
  if (cluster_ == nullptr) {
    ASSERT(pools_.empty());
    ASSERT(pools_to_drain_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    // TODO use custom reason
    callbacks.onPoolFailure(PoolFailureReason::RemoteConnectionFailure, nullptr);
    return nullptr;
  }
  // TODO(qinggniq) add custom load balancer context
  Upstream::HostConstSharedPtr host = cluster_->loadBalancer().chooseHost(nullptr);
  if (!host) {
    ENVOY_LOG(debug, "no host in cluster {}", cluster_name_);
    callbacks.onPoolFailure(PoolFailureReason::RemoteConnectionFailure, nullptr);
    return nullptr;
  }

  if (pools_.find(host) == pools_.end()) {
    pools_[host] =
        std::make_unique<ConnPoolImpl>(dispatcher_, host, Upstream::ResourcePriority::Default,
                                       nullptr, nullptr, parent_.cluster_manager_state_);
  }
  return pools_[host]->newConnection(callbacks);
}

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy