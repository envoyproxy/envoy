#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/stats/utility.h"
#include "source/extensions/filters/network/redis_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {
namespace {
// null_pool_callbacks is used for requests that must be filtered and not redirected such as
// "asking".
Common::Redis::Client::DoNothingPoolCallbacks null_client_callbacks;

const Common::Redis::RespValue& getRequest(const RespVariant& request) {
  if (request.index() == 0) {
    return absl::get<const Common::Redis::RespValue>(request);
  } else {
    return *(absl::get<Common::Redis::RespValueConstSharedPtr>(request));
  }
}
} // namespace

InstanceImpl::InstanceImpl(
    const std::string& cluster_name, Upstream::ClusterManager& cm,
    Common::Redis::Client::ClientFactory& client_factory, ThreadLocal::SlotAllocator& tls,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings&
        config,
    Api::Api& api, Stats::ScopePtr&& stats_scope,
    const Common::Redis::RedisCommandStatsSharedPtr& redis_command_stats,
    Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager)
    : cluster_name_(cluster_name), cm_(cm), client_factory_(client_factory),
      tls_(tls.allocateSlot()), config_(new Common::Redis::Client::ConfigImpl(config)), api_(api),
      stats_scope_(std::move(stats_scope)),
      redis_command_stats_(redis_command_stats), redis_cluster_stats_{REDIS_CLUSTER_STATS(
                                                     POOL_COUNTER(*stats_scope_))},
      refresh_manager_(std::move(refresh_manager)) {}

void InstanceImpl::init() {
  // Note: `this` and `cluster_name` have a a lifetime of the filter.
  // That may be shorter than the tls callback if the listener is torn down shortly after it is
  // created. We use a weak pointer to make sure this object outlives the tls callbacks.
  std::weak_ptr<InstanceImpl> this_weak_ptr = this->shared_from_this();
  tls_->set(
      [this_weak_ptr](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        if (auto this_shared_ptr = this_weak_ptr.lock()) {
          return std::make_shared<ThreadLocalPool>(this_shared_ptr, dispatcher,
                                                   this_shared_ptr->cluster_name_);
        }
        return nullptr;
      });
}

// This method is always called from a InstanceSharedPtr we don't have to worry about tls_->getTyped
// failing due to InstanceImpl going away.
Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequest(const std::string& key, RespVariant&& request, PoolCallbacks& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().makeRequest(key, std::move(request), callbacks);
}

// This method is always called from a InstanceSharedPtr we don't have to worry about tls_->getTyped
// failing due to InstanceImpl going away.
Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequestToHost(const std::string& host_address,
                                const Common::Redis::RespValue& request,
                                Common::Redis::Client::ClientCallbacks& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().makeRequestToHost(host_address, request, callbacks);
}

InstanceImpl::ThreadLocalPool::ThreadLocalPool(std::shared_ptr<InstanceImpl> parent,
                                               Event::Dispatcher& dispatcher,
                                               std::string cluster_name)
    : parent_(parent), dispatcher_(dispatcher), cluster_name_(std::move(cluster_name)),
      drain_timer_(dispatcher.createTimer([this]() -> void { drainClients(); })),
      is_redis_cluster_(false), client_factory_(parent->client_factory_), config_(parent->config_),
      stats_scope_(parent->stats_scope_), redis_command_stats_(parent->redis_command_stats_),
      redis_cluster_stats_(parent->redis_cluster_stats_),
      refresh_manager_(parent->refresh_manager_) {
  cluster_update_handle_ = parent->cm_.addThreadLocalClusterUpdateCallbacks(*this);
  Upstream::ThreadLocalCluster* cluster = parent->cm_.getThreadLocalCluster(cluster_name_);
  if (cluster != nullptr) {
    onClusterAddOrUpdateNonVirtual(*cluster);
  }
}

InstanceImpl::ThreadLocalPool::~ThreadLocalPool() {
  while (!pending_requests_.empty()) {
    pending_requests_.pop_front();
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
  // Update username and password when cluster updates.
  auth_username_ = ProtocolOptionsConfigImpl::authUsername(cluster_->info(), shared_parent->api_);
  auth_password_ = ProtocolOptionsConfigImpl::authPassword(cluster_->info(), shared_parent->api_);
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
                      cluster_type.has_value() && cluster_type->name() == "envoy.clusters.redis";
}

void InstanceImpl::ThreadLocalPool::onClusterRemoval(const std::string& cluster_name) {
  if (cluster_name != cluster_name_) {
    return;
  }

  // Treat cluster removal as a removal of all hosts. Close all connections and fail all pending
  // requests.
  host_set_member_update_cb_handle_ = nullptr;
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
    client->redis_client_ =
        client_factory_.create(host, dispatcher_, *config_, redis_command_stats_, *(stats_scope_),
                               auth_username_, auth_password_);
    client->redis_client_->addConnectionCallbacks(*client);
  }
  return client;
}

Common::Redis::Client::PoolRequest*
InstanceImpl::ThreadLocalPool::makeRequest(const std::string& key, RespVariant&& request,
                                           PoolCallbacks& callbacks) {
  if (cluster_ == nullptr) {
    ASSERT(client_map_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    return nullptr;
  }

  Clusters::Redis::RedisLoadBalancerContextImpl lb_context(key, config_->enableHashtagging(),
                                                           is_redis_cluster_, getRequest(request),
                                                           config_->readPolicy());
  Upstream::HostConstSharedPtr host = cluster_->loadBalancer().chooseHost(&lb_context);
  if (!host) {
    ENVOY_LOG(debug, "host not found: '{}'", key);
    return nullptr;
  }
  pending_requests_.emplace_back(*this, std::move(request), callbacks);
  PendingRequest& pending_request = pending_requests_.back();
  ThreadLocalActiveClientPtr& client = this->threadLocalActiveClient(host);
  pending_request.request_handler_ = client->redis_client_->makeRequest(
      getRequest(pending_request.incoming_request_), pending_request);
  if (pending_request.request_handler_) {
    return &pending_request;
  } else {
    onRequestCompleted();
    return nullptr;
  }
}

Common::Redis::Client::PoolRequest* InstanceImpl::ThreadLocalPool::makeRequestToHost(
    const std::string& host_address, const Common::Redis::RespValue& request,
    Common::Redis::Client::ClientCallbacks& callbacks) {
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
    uint32_t ip_port_number;
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
    if (created_via_redirect_hosts_.size() == config_->maxUpstreamUnknownConnections()) {
      // Too many upstream connections to unknown hosts have been created.
      redis_cluster_stats_.max_upstream_unknown_connections_reached_.inc();
      return nullptr;
    }
    if (!ipv6) {
      // Only create an IPv4 address instance if we need a new Upstream::HostImpl.
      const auto ip_port = absl::string_view(host_address).substr(colon_pos + 1);
      uint32_t ip_port_number;
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
        cluster_->info(), "", address_ptr, nullptr, 1, envoy::config::core::v3::Locality(),
        envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
        envoy::config::core::v3::UNKNOWN, dispatcher_.timeSource())};
    host_address_map_[host_address_map_key] = new_host;
    created_via_redirect_hosts_.push_back(new_host);
    it = host_address_map_.find(host_address_map_key);
  }

  ThreadLocalActiveClientPtr& client = threadLocalActiveClient(it->second);

  return client->redis_client_->makeRequest(request, callbacks);
}

void InstanceImpl::ThreadLocalPool::onRequestCompleted() {
  ASSERT(!pending_requests_.empty());

  // The response we got might not be in order, so flush out what we can. (A new response may
  // unlock several out of order responses).
  while (!pending_requests_.empty() && !pending_requests_.front().request_handler_) {
    pending_requests_.pop_front();
  }
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
            parent_.redis_cluster_stats_.upstream_cx_drained_.inc();
          }
          parent_.dispatcher_.deferredDelete(std::move(redis_client_));
          parent_.clients_to_drain_.erase(it);
          break;
        }
      }
    }
  }
}

InstanceImpl::PendingRequest::PendingRequest(InstanceImpl::ThreadLocalPool& parent,
                                             RespVariant&& incoming_request,
                                             PoolCallbacks& pool_callbacks)
    : parent_(parent), incoming_request_(std::move(incoming_request)),
      pool_callbacks_(pool_callbacks) {}

InstanceImpl::PendingRequest::~PendingRequest() {
  if (request_handler_) {
    request_handler_->cancel();
    request_handler_ = nullptr;
    // If we have to cancel the request on the client, then we'll treat this as failure for pool
    // callback
    pool_callbacks_.onFailure();
  }
}

void InstanceImpl::PendingRequest::onResponse(Common::Redis::RespValuePtr&& response) {
  request_handler_ = nullptr;
  pool_callbacks_.onResponse(std::move(response));
  parent_.onRequestCompleted();
}

void InstanceImpl::PendingRequest::onFailure() {
  request_handler_ = nullptr;
  pool_callbacks_.onFailure();
  parent_.refresh_manager_->onFailure(parent_.cluster_name_);
  parent_.onRequestCompleted();
}

bool InstanceImpl::PendingRequest::onRedirection(Common::Redis::RespValuePtr&& value,
                                                 const std::string& host_address,
                                                 bool ask_redirection) {
  // Prepend request with an asking command if redirected via an ASK error. The returned handle is
  // not important since there is no point in being able to cancel the request. The use of
  // null_pool_callbacks ensures the transparent filtering of the Redis server's response to the
  // "asking" command; this is fine since the server either responds with an OK or an error message
  // if cluster support is not enabled (in which case we should not get an ASK redirection error).
  if (ask_redirection &&
      !parent_.makeRequestToHost(host_address, Common::Redis::Utility::AskingRequest::instance(),
                                 null_client_callbacks)) {
    onResponse(std::move(value));
    return false;
  }
  request_handler_ = parent_.makeRequestToHost(host_address, getRequest(incoming_request_), *this);
  if (!request_handler_) {
    onResponse(std::move(value));
    return false;
  } else {
    parent_.refresh_manager_->onRedirection(parent_.cluster_name_);
    return true;
  }
}

void InstanceImpl::PendingRequest::cancel() {
  request_handler_->cancel();
  request_handler_ = nullptr;
  parent_.onRequestCompleted();
}

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
