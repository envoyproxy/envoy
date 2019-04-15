#include "redis_cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

RedisCluster::RedisCluster(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::redis::RedisClusterConfig& redisCluster,
    NetworkFilters::Common::Redis::Client::ClientFactory& redis_client_factory,
    Upstream::ClusterManager& clusterManager, Runtime::Loader& runtime,
    Network::DnsResolverSharedPtr dns_resolver,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api),
      cluster_manager_(clusterManager),
      cluster_refresh_rate_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redisCluster, cluster_refresh_rate, 5000))),
      cluster_refresh_timeout_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redisCluster, cluster_refresh_timeout, 3000))),
      dispatcher_(factory_context.dispatcher()), dns_resolver_(std::move(dns_resolver)),
      load_assignment_(cluster.has_load_assignment()
                           ? cluster.load_assignment()
                           : Config::Utility::translateClusterHosts(cluster.hosts())),
      local_info_(factory_context.localInfo()), random_(factory_context.random()),
      redis_discovery_session_(
          std::make_unique<RedisDiscoverySession>(*this, redis_client_factory)) {

  dns_lookup_family_ = Upstream::getDnsLookupFamilyFromCluster(cluster);
  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& host = lb_endpoint.endpoint().address();
      dns_discovery_resolve_targets_.emplace_back(new DnsDiscoveryResolveTarget(
          *this, host.socket_address().address(), locality_lb_endpoint, lb_endpoint));
    }
  }
};

void RedisCluster::startPreInit() {
  for (const DnsDiscoveryResolveTargetPtr& target : dns_discovery_resolve_targets_) {
    target->startResolve();
  }
}

void RedisCluster::updateAllHosts(const Upstream::HostVector& hosts_added,
                                  const Upstream::HostVector& hosts_removed,
                                  uint32_t current_priority) {
  Upstream::PriorityStateManager priority_state_manager(*this, local_info_, nullptr);

  priority_state_manager.initializePriorityFor(redis_discovery_session_->locality_lb_endpoint_);
  for (const Upstream::HostSharedPtr& host : redis_discovery_session_->hosts_) {
    if (redis_discovery_session_->locality_lb_endpoint_.priority() == current_priority) {
      priority_state_manager.registerHostForPriority(
          host, redis_discovery_session_->locality_lb_endpoint_);
    }
  }

  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt);
}

// DnsDiscoveryResolveTarget
RedisCluster::DnsDiscoveryResolveTarget::DnsDiscoveryResolveTarget(
    RedisCluster& parent, const std::string& dns_address,
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint)
    : parent_(parent), dns_address_(dns_address), locality_lb_endpoint_(locality_lb_endpoint),
      lb_endpoint_(lb_endpoint) {}

RedisCluster::DnsDiscoveryResolveTarget::~DnsDiscoveryResolveTarget() {
  if (active_query_) {
    active_query_->cancel();
  }
}

void RedisCluster::DnsDiscoveryResolveTarget::startResolve() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);

  active_query_ = parent_.dns_resolver_->resolve(
      dns_address_, parent_.dns_lookup_family_,
      [this](const std::list<Network::Address::InstanceConstSharedPtr>&& address_list) -> void {
        active_query_ = nullptr;
        ENVOY_LOG(trace, "async DNS resolution complete for {}", dns_address_);
        parent_.redis_discovery_session_->registerDiscoveryAddress(address_list);
        parent_.redis_discovery_session_->startResolve();
      });
}

// RedisCluster
RedisCluster::RedisDiscoverySession::RedisDiscoverySession(
    Envoy::Extensions::Clusters::Redis::RedisCluster& parent,
    NetworkFilters::Common::Redis::Client::ClientFactory& client_factory)
    : parent_(parent),
      resolve_timer_(parent.dispatcher_.createTimer([this]() -> void { startResolve(); })),
      client_factory_(client_factory) {}

namespace {
Network::Address::InstanceConstSharedPtr
ProcessCluster(const NetworkFilters::Common::Redis::RespValue& value) {
  ASSERT(value.type() == NetworkFilters::Common::Redis::RespType::Array);
  ASSERT(value.asArray().size() >= 2);
  std::string address = value.asArray()[0].asString();
  bool ipv6 = (address.find(":") != std::string::npos);
  if (ipv6) {
    return std::make_shared<Network::Address::Ipv6Instance>(address,
                                                            value.asArray()[1].asInteger());
  }
  return std::make_shared<Network::Address::Ipv4Instance>(address, value.asArray()[1].asInteger());
}
} // namespace

RedisCluster::RedisDiscoverySession::~RedisDiscoverySession() {
  if (current_request_) {
    current_request_->cancel();
    current_request_ = nullptr;
  }

  if (client_) {
    client_->close();
  }
}

void RedisCluster::RedisDiscoverySession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // This should only happen after any active requests have been failed/cancelled.
    ASSERT(!current_request_);
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void RedisCluster::RedisDiscoverySession::registerDiscoveryAddress(
    const std::list<Envoy::Network::Address::InstanceConstSharedPtr>& address_list) {
  discovery_address_list_.insert(discovery_address_list_.end(), address_list.begin(),
                                 address_list.end());
}

void RedisCluster::RedisDiscoverySession::startResolve() {
  parent_.info_->stats().update_attempt_.inc();
  // If a resolution is currently in progress, skip it.
  if (current_request_) {
    return;
  }

  // If hosts is empty, we haven't received a successful result from the CLUSTER SLOTS call yet.
  // So, pick a random discovery address from dns and make a request.
  Upstream::HostSharedPtr host;
  if (hosts_.empty()) {
    const int rand_idx = parent_.random_.random() % discovery_address_list_.size();
    auto it = discovery_address_list_.begin();
    std::next(it, rand_idx);
    host = Upstream::HostSharedPtr{new RedisHost(parent_.info(), "", *it, parent_, true)};
  } else {
    const int rand_idx = parent_.random_.random() % hosts_.size();
    host = hosts_[rand_idx];
  }

  if (!client_) {
    client_ = client_factory_.create(host, parent_.dispatcher_, *this);
    client_->addConnectionCallbacks(*this);
  }

  current_request_ = client_->makeRequest(ClusterSlotsRequest::instance_, *this);
}

void RedisCluster::RedisDiscoverySession::onResponse(
    NetworkFilters::Common::Redis::RespValuePtr&& value) {
  current_request_ = nullptr;
  // TODO(hyang): Move to use connection pool for the cluster and avoid closing the connection
  // each time.
  client_->close();

  if (value->type() != NetworkFilters::Common::Redis::RespType::Array) {
    ENVOY_LOG(warn, "Unexpected response to cluster slot command: {}", value->toString());
    parent_.info_->stats().update_no_rebuild_.inc();
    return;
  }

  Upstream::HostVector new_hosts;
  std::array<std::string, 16384> slots_;

  for (const NetworkFilters::Common::Redis::RespValue& part : value->asArray()) {
    const std::vector<NetworkFilters::Common::Redis::RespValue>& slot_range = part.asArray();
    ASSERT(slot_range.size() >= 3);

    // Field 0: Start slot range
    auto start_field = slot_range[0].asInteger();

    // Field 1: End slot range
    auto end_field = slot_range[1].asInteger();

    // Field 2: Master address for slot range
    // TODO(hyang): For now we're only adding the master node for each slot. When we're ready to
    //  send requests to replica nodes, we need to add subsequent address in the response as
    //  replica nodes.
    auto master_address = ProcessCluster(slot_range[2]);
    // Add to the slot
    for (auto i = start_field; i <= end_field; ++i) {
      slots_[i] = master_address->asString();
      // TODO: Investigate performance of alternative implementations, as this will be O(n)
      // where n is number of shards.
    }
    new_hosts.emplace_back(
        new RedisHost(parent_.info(), "", std::move(master_address), parent_, true));
  }

  std::unordered_map<std::string, Upstream::HostSharedPtr> updated_hosts;
  Upstream::HostVector hosts_added;
  Upstream::HostVector hosts_removed;
  if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed, updated_hosts,
                                    all_hosts_)) {
    ASSERT(std::all_of(hosts_.begin(), hosts_.end(), [&](const auto& host) {
      return host->priority() == locality_lb_endpoint_.priority();
    }));
    parent_.updateAllHosts(hosts_added, hosts_removed, locality_lb_endpoint_.priority());
  } else {
    parent_.info_->stats().update_no_rebuild_.inc();
  }

  all_hosts_ = std::move(updated_hosts);
  cluster_slots_map_.swap(slots_);

  // TODO(hyang): If there is an initialize callback, fire it now. Note that if the
  // cluster refers to multiple DNS names, this will return initialized after a single
  // DNS resolution completes. This is not perfect but is easier to code and it is unclear
  // if the extra complexity is needed so will start with this.
  parent_.onPreInitComplete();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

void RedisCluster::RedisDiscoverySession::onFailure() {
  current_request_ = nullptr;
  if (client_) {
    client_->close();
  }
  parent_.info()->stats().update_failure_.inc();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

RedisCluster::ClusterSlotsRequest RedisCluster::ClusterSlotsRequest::instance_;

Upstream::ClusterImplBaseSharedPtr RedisClusterFactory::createClusterWithConfig(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::redis::RedisClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Envoy::Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Envoy::Stats::ScopePtr&& stats_scope) {
  if (!cluster.has_cluster_type() ||
      cluster.cluster_type().name() != Extensions::Clusters::ClusterTypes::get().Redis) {
    throw EnvoyException("Redis cluster can only created with redis cluster type");
  }
  return std::make_shared<RedisCluster>(
      cluster, proto_config, NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_,
      context.clusterManager(), context.runtime(), selectDnsResolver(cluster, context),
      socket_factory_context, std::move(stats_scope), context.addedViaApi());
}

REGISTER_FACTORY(RedisClusterFactory, Upstream::ClusterFactory);

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
