#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/stats/utility.h"

#include "extensions/filters/network/redis_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {

InstanceImpl::InstanceImpl(
    const std::string& cluster_name, Upstream::ClusterManager& cm,
    Common::Redis::Client::ClientFactory& client_factory, ThreadLocal::SlotAllocator& tls,
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings& config,
    Api::Api& api, Stats::ScopePtr&& stats_scope,
    const Common::Redis::RedisCommandStatsSharedPtr& redis_command_stats,
    Extensions::Common::Redis::RedirectionManagerSharedPtr redirection_manager)
    : cluster_name_(cluster_name), cm_(cm), client_factory_(client_factory),
      tls_(tls.allocateSlot()), config_(config), api_(api), stats_scope_(std::move(stats_scope)),
      redis_command_stats_(redis_command_stats), redis_cluster_stats_{REDIS_CLUSTER_STATS(
                                                     POOL_COUNTER(*stats_scope_))},
      redirection_manager_(std::move(redirection_manager)) {
  tls_->set([this, cluster_name](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalPool>(*this, dispatcher, cluster_name);
  });
}

Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequest(const std::string& key, const Common::Redis::RespValue& request,
                          Common::Redis::Client::PoolCallbacks& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().makeRequest(key, request, callbacks);
}

Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequestToHost(const std::string& host_address,
                                const Common::Redis::RespValue& request,
                                Common::Redis::Client::PoolCallbacks& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().makeRequestToHost(host_address, request, callbacks);
}

InstanceImpl::ThreadLocalPool::ThreadLocalPool(InstanceImpl& parent, Event::Dispatcher& dispatcher,
                                               std::string cluster_name)
    : parent_(parent), dispatcher_(dispatcher), cluster_name_(std::move(cluster_name)),
      drain_timer_(dispatcher.createTimer([this]() -> void { drainClients(); })),
      is_redis_cluster_(false) {
  cluster_update_handle_ = parent_.cm_.addThreadLocalClusterUpdateCallbacks(*this);
  Upstream::ThreadLocalCluster* cluster = parent_.cm_.get(cluster_name_);
  if (cluster != nullptr) {
    auth_password_ = ProtocolOptionsConfigImpl::auth_password(cluster->info(), parent_.api_);
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
  while (!clients_to_drain_.empty()) {
    (*clients_to_drain_.begin())->redis_client_->close();
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

  // Figure out if the cluster associated with this ConnPool is a Redis cluster
  // with its own hash slot sharding scheme and ability to dynamically discover
  // its members. This is done once to minimize overhead in the data path, makeRequest() in
  // particular.
  Upstream::ClusterInfoConstSharedPtr info = cluster_->info();
  const auto& cluster_type = info->clusterType();
  is_redis_cluster_ = info->lbType() == Upstream::LoadBalancerType::ClusterProvided &&
                      cluster_type.has_value() &&
                      cluster_type->name() == Extensions::Clusters::ClusterTypes::get().Redis;
}

void InstanceImpl::ThreadLocalPool::onClusterRemoval(const std::string& cluster_name) {
  if (cluster_name != cluster_name_) {
    return;
  }

  // Treat cluster removal as a removal of all hosts. Close all connections and fail all pending
  // requests.
  if (host_set_member_update_cb_handle_ != nullptr) {
    host_set_member_update_cb_handle_->remove();
    host_set_member_update_cb_handle_ = nullptr;
  }
  while (!client_map_.empty()) {
    client_map_.begin()->second->redis_client_->close();
  }
  while (!clients_to_drain_.empty()) {
    (*clients_to_drain_.begin())->redis_client_->close();
  }

  cluster_ = nullptr;
  host_address_map_.clear();
}

void InstanceImpl::ThreadLocalPool::onHostsAdded(
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

void InstanceImpl::ThreadLocalPool::onHostsRemoved(
    const std::vector<Upstream::HostSharedPtr>& hosts_removed) {
  for (const auto& host : hosts_removed) {
    auto it = client_map_.find(host);
    if (it != client_map_.end()) {
      if (it->second->redis_client_->active()) {
        // Put the ThreadLocalActiveClient to the side to drain.
        clients_to_drain_.push_back(std::move(it->second));
        client_map_.erase(it);
        if (!drain_timer_->enabled()) {
          drain_timer_->enableTimer(std::chrono::seconds(1));
        }
      } else {
        // There are no pending requests so close the connection.
        it->second->redis_client_->close();
      }
    }
    // There is the possibility that multiple hosts with the same address
    // are registered in host_address_map_ given that hosts may be created
    // upon redirection or supplied as part of the cluster's definition.
    auto it2 = host_address_map_.find(host->address()->asString());
    if ((it2 != host_address_map_.end()) && (it2->second == host)) {
      host_address_map_.erase(it2);
    }
  }
}

void InstanceImpl::ThreadLocalPool::drainClients() {
  while (!clients_to_drain_.empty() && !(*clients_to_drain_.begin())->redis_client_->active()) {
    (*clients_to_drain_.begin())->redis_client_->close();
  }
  if (!clients_to_drain_.empty()) {
    drain_timer_->enableTimer(std::chrono::seconds(1));
  }
}

InstanceImpl::ThreadLocalActiveClientPtr&
InstanceImpl::ThreadLocalPool::threadLocalActiveClient(Upstream::HostConstSharedPtr host) {
  ThreadLocalActiveClientPtr& client = client_map_[host];
  if (!client) {
    client = std::make_unique<ThreadLocalActiveClient>(*this);
    client->host_ = host;
    client->redis_client_ = parent_.client_factory_.create(host, dispatcher_, parent_.config_,
                                                           parent_.redis_command_stats_,
                                                           *parent_.stats_scope_, auth_password_);
    client->redis_client_->addConnectionCallbacks(*client);
  }
  return client;
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

  Clusters::Redis::RedisLoadBalancerContextImpl lb_context(key, parent_.config_.enableHashtagging(),
                                                           is_redis_cluster_, request,
                                                           parent_.config_.readPolicy());
  Upstream::HostConstSharedPtr host = cluster_->loadBalancer().chooseHost(&lb_context);
  if (!host) {
    return nullptr;
  }

  ThreadLocalActiveClientPtr& client = threadLocalActiveClient(host);

  return client->redis_client_->makeRequest(request, callbacks);
}

Common::Redis::Client::PoolRequest*
InstanceImpl::ThreadLocalPool::makeRequestToHost(const std::string& host_address,
                                                 const Common::Redis::RespValue& request,
                                                 Common::Redis::Client::PoolCallbacks& callbacks) {
  if (cluster_ == nullptr) {
    ASSERT(client_map_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    return nullptr;
  }

  auto colon_pos = host_address.rfind(':');
  if ((colon_pos == std::string::npos) || (colon_pos == (host_address.size() - 1))) {
    return nullptr;
  }

  const std::string ip_address = host_address.substr(0, colon_pos);
  const bool ipv6 = (ip_address.find(':') != std::string::npos);
  std::string host_address_map_key;
  Network::Address::InstanceConstSharedPtr address_ptr;

  if (!ipv6) {
    host_address_map_key = host_address;
  } else {
    const auto ip_port = absl::string_view(host_address).substr(colon_pos + 1);
    uint64_t ip_port_number;
    if (!absl::SimpleAtoi(ip_port, &ip_port_number) || (ip_port_number > 65535)) {
      return nullptr;
    }
    try {
      address_ptr = std::make_shared<Network::Address::Ipv6Instance>(ip_address, ip_port_number);
    } catch (const EnvoyException&) {
      return nullptr;
    }
    host_address_map_key = address_ptr->asString();
  }

  auto it = host_address_map_.find(host_address_map_key);
  if (it == host_address_map_.end()) {
    // This host is not known to the cluster manager. Create a new host and insert it into the map.
    if (created_via_redirect_hosts_.size() == parent_.config_.maxUpstreamUnknownConnections()) {
      // Too many upstream connections to unknown hosts have been created.
      parent_.redis_cluster_stats_.max_upstream_unknown_connections_reached_.inc();
      return nullptr;
    }
    if (!ipv6) {
      // Only create an IPv4 address instance if we need a new Upstream::HostImpl.
      const auto ip_port = absl::string_view(host_address).substr(colon_pos + 1);
      uint64_t ip_port_number;
      if (!absl::SimpleAtoi(ip_port, &ip_port_number) || (ip_port_number > 65535)) {
        return nullptr;
      }
      try {
        address_ptr = std::make_shared<Network::Address::Ipv4Instance>(ip_address, ip_port_number);
      } catch (const EnvoyException&) {
        return nullptr;
      }
    }
    Upstream::HostSharedPtr new_host{new Upstream::HostImpl(
        cluster_->info(), "", address_ptr, envoy::api::v2::core::Metadata::default_instance(), 1,
        envoy::api::v2::core::Locality(),
        envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0,
        envoy::api::v2::core::HealthStatus::UNKNOWN)};
    host_address_map_[host_address_map_key] = new_host;
    created_via_redirect_hosts_.push_back(new_host);
    it = host_address_map_.find(host_address_map_key);
  }

  ThreadLocalActiveClientPtr& client = threadLocalActiveClient(it->second);

  return client->redis_client_->makeRequest(request, callbacks);
}

void InstanceImpl::ThreadLocalActiveClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    auto client_to_delete = parent_.client_map_.find(host_);
    if (client_to_delete != parent_.client_map_.end()) {
      parent_.dispatcher_.deferredDelete(std::move(redis_client_));
      parent_.client_map_.erase(client_to_delete);
    } else {
      for (auto it = parent_.clients_to_drain_.begin(); it != parent_.clients_to_drain_.end();
           it++) {
        if ((*it).get() == this) {
          if (!redis_client_->active()) {
            parent_.parent_.redis_cluster_stats_.upstream_cx_drained_.inc();
          }
          parent_.dispatcher_.deferredDelete(std::move(redis_client_));
          parent_.clients_to_drain_.erase(it);
          break;
        }
      }
    }
  }
}

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
