#include "redis_cluster.h"

#include <err.h>

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
      dns_lookup_family_(Upstream::getDnsLookupFamilyFromCluster(cluster)),
      load_assignment_(cluster.has_load_assignment()
                           ? cluster.load_assignment()
                           : Config::Utility::translateClusterHosts(cluster.hosts())),
      local_info_(factory_context.localInfo()), random_(factory_context.random()),
      redis_discovery_session_(
          std::make_unique<RedisDiscoverySession>(*this, redis_client_factory)) {
  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& host = lb_endpoint.endpoint().address();
      dns_discovery_resolve_targets_.emplace_back(new DnsDiscoveryResolveTarget(
          *this, host.socket_address().address(), host.socket_address().port_value(),
          locality_lb_endpoint, lb_endpoint));
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
    RedisCluster& parent, const std::string& dns_address, const uint32_t port,
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint)
    : parent_(parent), dns_address_(dns_address), port_(port),
      locality_lb_endpoint_(locality_lb_endpoint), lb_endpoint_(lb_endpoint) {}

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
        parent_.redis_discovery_session_->registerDiscoveryAddress(address_list, port_);
        parent_.redis_discovery_session_->startResolve();
      });
}

// RedisCluster
RedisCluster::RedisDiscoverySession::RedisDiscoverySession(
    Envoy::Extensions::Clusters::Redis::RedisCluster& parent,
    NetworkFilters::Common::Redis::Client::ClientFactory& client_factory)
    : parent_(parent), dispatcher_(parent.dispatcher_),
      resolve_timer_(parent.dispatcher_.createTimer([this]() -> void { startResolve(); })),
      client_factory_(client_factory), buffer_timeout_(0) {}

namespace {
// Convert the cluster slot IP/Port response to and address, return null if the response does not
// match the expected type
Network::Address::InstanceConstSharedPtr
ProcessCluster(const NetworkFilters::Common::Redis::RespValue& value) {
  if (value.type() != NetworkFilters::Common::Redis::RespType::Array) {
    return nullptr;
  }
  auto& array = value.asArray();

  if (array.size() < 2 || array[0].type() != NetworkFilters::Common::Redis::RespType::BulkString ||
      array[1].type() != NetworkFilters::Common::Redis::RespType::Integer) {
    return nullptr;
  }

  std::string address = array[0].asString();
  bool ipv6 = (address.find(":") != std::string::npos);
  if (ipv6) {
    return std::make_shared<Network::Address::Ipv6Instance>(address, array[1].asInteger());
  }
  return std::make_shared<Network::Address::Ipv4Instance>(address, array[1].asInteger());
}
} // namespace

RedisCluster::RedisDiscoverySession::~RedisDiscoverySession() {
  if (current_request_) {
    current_request_->cancel();
    current_request_ = nullptr;
  }

  while (!client_map_.empty()) {
    client_map_.begin()->second->client_->close();
  }
}

void RedisCluster::RedisDiscoveryClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    auto client_to_delete = parent_.client_map_.find(host_);
    ASSERT(client_to_delete != parent_.client_map_.end());
    parent_.dispatcher_.deferredDelete(std::move(client_to_delete->second->client_));
    parent_.client_map_.erase(client_to_delete);
  }
}

void RedisCluster::RedisDiscoverySession::registerDiscoveryAddress(
    const std::list<Envoy::Network::Address::InstanceConstSharedPtr>& address_list,
    const uint32_t port) {
  // Since the address from DNS does not have port, we need to make a new address that has port in
  // it.
  for (const Network::Address::InstanceConstSharedPtr& address : address_list) {
    ASSERT(address != nullptr);
    discovery_address_list_.push_back(Network::Utility::getAddressWithPort(*address, port));
  }
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

  current_host_address_ = host->address()->asString();
  RedisDiscoveryClientPtr& client = client_map_[current_host_address_];
  if (!client) {
    client = std::make_unique<RedisDiscoveryClient>(*this);
    client->host_ = current_host_address_;
    client->client_ = client_factory_.create(host, dispatcher_, *this);
    client->client_->addConnectionCallbacks(*client);
  }

  current_request_ = client->client_->makeRequest(ClusterSlotsRequest::instance_, *this);
}

void RedisCluster::RedisDiscoverySession::onResponse(
    NetworkFilters::Common::Redis::RespValuePtr&& value) {
  current_request_ = nullptr;

  if (value->type() != NetworkFilters::Common::Redis::RespType::Array) {
    onUnexpectedResponse(value);
    return;
  }

  Upstream::HostVector new_hosts;
  std::array<Upstream::HostSharedPtr, 16384> slots_;

  // loop through the cluster slot response and error checks for each field
  for (const NetworkFilters::Common::Redis::RespValue& part : value->asArray()) {
    if (part.type() != NetworkFilters::Common::Redis::RespType::Array) {
      onUnexpectedResponse(value);
      return;
    }
    const std::vector<NetworkFilters::Common::Redis::RespValue>& slot_range = part.asArray();
    if (slot_range.size() < 3 ||
        slot_range[0].type() !=
            NetworkFilters::Common::Redis::RespType::Integer || // start slot range as an integer
        slot_range[1].type() !=
            NetworkFilters::Common::Redis::RespType::Integer || // end slot range as an integer
        slot_range[2].type() !=
            NetworkFilters::Common::Redis::RespType::Array) { // master ip and port pair as an array
      onUnexpectedResponse(value);
      return;
    }

    // Field 2: Master address for slot range
    // TODO(hyang): For now we're only adding the master node for each slot. When we're ready to
    //  send requests to replica nodes, we need to add subsequent address in the response as
    //  replica nodes.
    auto master_address = ProcessCluster(slot_range[2]);
    if (!master_address) {
      onUnexpectedResponse(value);
      return;
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

  // build the slot lookup array in a second pass, so that it can referenced the updated_hosts.
  for (const NetworkFilters::Common::Redis::RespValue& part : value->asArray()) {
    const std::vector<NetworkFilters::Common::Redis::RespValue>& slot_range = part.asArray();

    // Field 0: Start slot range
    auto start_field = slot_range[0].asInteger();

    // Field 1: End slot range
    auto end_field = slot_range[1].asInteger();

    // Field 2: Master address for slot range
    auto master_address = ProcessCluster(slot_range[2]);
    auto host = updated_hosts.find(master_address->asString());
    ASSERT(host != updated_hosts.end(), "we expect all address to be found in the updated_hosts");
    // Add to the slot
    for (auto i = start_field; i <= end_field; ++i) {
      slots_[i] = host->second;
    }
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

void RedisCluster::RedisDiscoverySession::onUnexpectedResponse(
    const NetworkFilters::Common::Redis::RespValuePtr& value) {
  ENVOY_LOG(warn, "Unexpected response to cluster slot command: {}", value->toString());
  this->parent_.info_->stats().update_failure_.inc();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

void RedisCluster::RedisDiscoverySession::onFailure() {
  current_request_ = nullptr;
  if (!current_host_address_.empty()) {
    auto client_to_delete = client_map_.find(current_host_address_);
    if (client_to_delete != client_map_.end()) {
      client_to_delete->second->client_->close();
    }
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
