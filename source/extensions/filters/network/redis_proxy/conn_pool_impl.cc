#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {

InstanceImpl::InstanceImpl(
    const std::string& cluster_name, Upstream::ClusterManager& cm,
    Common::Redis::Client::ClientFactory& client_factory, ThreadLocal::SlotAllocator& tls,
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings& config)
    : cm_(cm), client_factory_(client_factory), tls_(tls.allocateSlot()), config_(config) {
  tls_->set([this, cluster_name](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalPool>(*this, dispatcher, cluster_name);
  });
}

Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequest(const std::string& key, const Common::Redis::RespValue& value,
                          Common::Redis::Client::PoolCallbacks& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().makeRequest(key, value, callbacks);
}

InstanceImpl::ThreadLocalPool::ThreadLocalPool(InstanceImpl& parent, Event::Dispatcher& dispatcher,
                                               std::string cluster_name)
    : parent_(parent), dispatcher_(dispatcher), cluster_name_(std::move(cluster_name)) {

  cluster_update_handle_ = parent_.cm_.addThreadLocalClusterUpdateCallbacks(*this);
  Upstream::ThreadLocalCluster* cluster = parent_.cm_.get(cluster_name_);
  if (cluster != nullptr) {
    onClusterAddOrUpdateNonVirtual(*cluster);
  }
}

InstanceImpl::ThreadLocalPool::~ThreadLocalPool() {
  if (host_set_member_update_cb_handle_ != nullptr) {
    host_set_member_update_cb_handle_->remove();
  }
  while (!client_map_.empty()) {
    client_map_.begin()->second->redis_client_->close();
  }
}

void InstanceImpl::ThreadLocalPool::onClusterAddOrUpdateNonVirtual(
    Upstream::ThreadLocalCluster& cluster) {
  if (cluster.info()->name() != cluster_name_) {
    return;
  }

  if (cluster_ != nullptr) {
    // Treat an update as a removal followed by an add.
    onClusterRemoval(cluster_name_);
  }

  ASSERT(cluster_ == nullptr);
  cluster_ = &cluster;
  ASSERT(host_set_member_update_cb_handle_ == nullptr);
  host_set_member_update_cb_handle_ = cluster_->prioritySet().addMemberUpdateCb(
      [this](const std::vector<Upstream::HostSharedPtr>&,
             const std::vector<Upstream::HostSharedPtr>& hosts_removed) -> void {
        onHostsRemoved(hosts_removed);
      });
}

void InstanceImpl::ThreadLocalPool::onClusterRemoval(const std::string& cluster_name) {
  if (cluster_name != cluster_name_) {
    return;
  }

  // Treat cluster removal as a removal of all hosts. Close all connections and fail all pending
  // requests.
  while (!client_map_.empty()) {
    client_map_.begin()->second->redis_client_->close();
  }

  cluster_ = nullptr;
  host_set_member_update_cb_handle_ = nullptr;
}

void InstanceImpl::ThreadLocalPool::onHostsRemoved(
    const std::vector<Upstream::HostSharedPtr>& hosts_removed) {
  for (const auto& host : hosts_removed) {
    auto it = client_map_.find(host);
    if (it != client_map_.end()) {
      // We don't currently support any type of draining for redis connections. If a host is gone,
      // we just close the connection. This will fail any pending requests.
      it->second->redis_client_->close();
    }
  }
}

Common::Redis::Client::PoolRequest*
InstanceImpl::ThreadLocalPool::makeRequest(const std::string& key,
                                           const Common::Redis::RespValue& request,
                                           Common::Redis::Client::PoolCallbacks& callbacks) {
  if (cluster_ == nullptr) {
    ASSERT(client_map_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    return nullptr;
  }

  LbContextImpl lb_context(key, parent_.config_.enableHashtagging());
  Upstream::HostConstSharedPtr host = cluster_->loadBalancer().chooseHost(&lb_context);
  if (!host) {
    return nullptr;
  }

  ThreadLocalActiveClientPtr& client = client_map_[host];
  if (!client) {
    client = std::make_unique<ThreadLocalActiveClient>(*this);
    client->host_ = host;
    client->redis_client_ = parent_.client_factory_.create(host, dispatcher_, parent_.config_);
    client->redis_client_->addConnectionCallbacks(*client);
  }

  return client->redis_client_->makeRequest(request, callbacks);
}

void InstanceImpl::ThreadLocalActiveClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    auto client_to_delete = parent_.client_map_.find(host_);
    ASSERT(client_to_delete != parent_.client_map_.end());
    parent_.dispatcher_.deferredDelete(std::move(client_to_delete->second->redis_client_));
    parent_.client_map_.erase(client_to_delete);
  }
}

// Inspired by the redis-cluster hashtagging algorithm
// https://redis.io/topics/cluster-spec#keys-hash-tags
absl::string_view InstanceImpl::LbContextImpl::hashtag(absl::string_view v, bool enabled) {
  if (!enabled) {
    return v;
  }

  auto start = v.find('{');
  if (start == std::string::npos) {
    return v;
  }

  auto end = v.find('}', start);
  if (end == std::string::npos || end == start + 1) {
    return v;
  }

  return v.substr(start + 1, end - start - 1);
}

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
