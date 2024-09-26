#include "source/extensions/clusters/redis/redis_cluster.h"

#include <cstdint>
#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.h"
#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.validate.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

absl::StatusOr<std::unique_ptr<RedisCluster>> RedisCluster::create(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::redis::v3::RedisClusterConfig& redis_cluster,
    Upstream::ClusterFactoryContext& context,
    NetworkFilters::Common::Redis::Client::ClientFactory& client_factory,
    Network::DnsResolverSharedPtr dns_resolver, ClusterSlotUpdateCallBackSharedPtr factory) {
  absl::Status creation_status = absl::OkStatus();
  std::unique_ptr<RedisCluster> ret = absl::WrapUnique(new RedisCluster(
      cluster, redis_cluster, context, client_factory, dns_resolver, factory, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

RedisCluster::RedisCluster(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::redis::v3::RedisClusterConfig& redis_cluster,
    Upstream::ClusterFactoryContext& context,
    NetworkFilters::Common::Redis::Client::ClientFactory& redis_client_factory,
    Network::DnsResolverSharedPtr dns_resolver, ClusterSlotUpdateCallBackSharedPtr lb_factory,
    absl::Status& creation_status)
    : Upstream::BaseDynamicClusterImpl(cluster, context, creation_status),
      cluster_manager_(context.clusterManager()),
      cluster_refresh_rate_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redis_cluster, cluster_refresh_rate, 5000))),
      cluster_refresh_timeout_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redis_cluster, cluster_refresh_timeout, 3000))),
      redirect_refresh_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(redis_cluster, redirect_refresh_interval, 5000))),
      redirect_refresh_threshold_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(redis_cluster, redirect_refresh_threshold, 5)),
      failure_refresh_threshold_(redis_cluster.failure_refresh_threshold()),
      host_degraded_refresh_threshold_(redis_cluster.host_degraded_refresh_threshold()),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      dns_resolver_(std::move(dns_resolver)),
      dns_lookup_family_(Upstream::getDnsLookupFamilyFromCluster(cluster)),
      load_assignment_(cluster.load_assignment()),
      local_info_(context.serverFactoryContext().localInfo()),
      random_(context.serverFactoryContext().api().randomGenerator()),
      redis_discovery_session_(
          std::make_shared<RedisDiscoverySession>(*this, redis_client_factory)),
      lb_factory_(std::move(lb_factory)),
      auth_username_(NetworkFilters::RedisProxy::ProtocolOptionsConfigImpl::authUsername(
          info(), context.serverFactoryContext().api())),
      auth_password_(NetworkFilters::RedisProxy::ProtocolOptionsConfigImpl::authPassword(
          info(), context.serverFactoryContext().api())),
      cluster_name_(cluster.name()),
      refresh_manager_(Common::Redis::getClusterRefreshManager(
          context.serverFactoryContext().singletonManager(),
          context.serverFactoryContext().mainThreadDispatcher(), context.clusterManager(),
          context.serverFactoryContext().api().timeSource())),
      registration_handle_(refresh_manager_->registerCluster(
          cluster_name_, redirect_refresh_interval_, redirect_refresh_threshold_,
          failure_refresh_threshold_, host_degraded_refresh_threshold_, [&]() {
            redis_discovery_session_->resolve_timer_->enableTimer(std::chrono::milliseconds(0));
          })) {
  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& host = lb_endpoint.endpoint().address();
      dns_discovery_resolve_targets_.emplace_back(new DnsDiscoveryResolveTarget(
          *this, host.socket_address().address(), host.socket_address().port_value()));
    }
  }
}

void RedisCluster::startPreInit() {
  for (const DnsDiscoveryResolveTargetPtr& target : dns_discovery_resolve_targets_) {
    target->startResolveDns();
  }
  if (!wait_for_warm_on_init_) {
    onPreInitComplete();
  }
}

void RedisCluster::updateAllHosts(const Upstream::HostVector& hosts_added,
                                  const Upstream::HostVector& hosts_removed,
                                  uint32_t current_priority) {
  Upstream::PriorityStateManager priority_state_manager(*this, local_info_, nullptr, random_);

  auto locality_lb_endpoint = localityLbEndpoint();
  priority_state_manager.initializePriorityFor(locality_lb_endpoint);
  for (const Upstream::HostSharedPtr& host : hosts_) {
    if (locality_lb_endpoint.priority() == current_priority) {
      priority_state_manager.registerHostForPriority(host, locality_lb_endpoint);
    }
  }

  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt, absl::nullopt, absl::nullopt);
}

void RedisCluster::onClusterSlotUpdate(ClusterSlotsSharedPtr&& slots) {
  Upstream::HostVector new_hosts;
  absl::flat_hash_set<std::string> all_new_hosts;

  for (const ClusterSlot& slot : *slots) {
    if (all_new_hosts.count(slot.primary()->asString()) == 0) {
      new_hosts.emplace_back(new RedisHost(info(), "", slot.primary(), *this, true, time_source_));
      all_new_hosts.emplace(slot.primary()->asString());
    }
    for (auto const& replica : slot.replicas()) {
      if (all_new_hosts.count(replica.first) == 0) {
        new_hosts.emplace_back(
            new RedisHost(info(), "", replica.second, *this, false, time_source_));
        all_new_hosts.emplace(replica.first);
      }
    }
  }

  // Get the map of all the latest existing hosts, which is used to filter out the existing
  // hosts in the process of updating cluster memberships.
  Upstream::HostMapConstSharedPtr all_hosts = priority_set_.crossPriorityHostMap();
  ASSERT(all_hosts != nullptr);

  Upstream::HostVector hosts_added;
  Upstream::HostVector hosts_removed;
  const bool host_updated = updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed,
                                                  *all_hosts, all_new_hosts);

  // Create a map containing all the latest hosts to determine whether the slots are updated.
  Upstream::HostMap updated_hosts = *all_hosts;
  for (const auto& host : hosts_removed) {
    updated_hosts.erase(host->address()->asString());
  }
  for (const auto& host : hosts_added) {
    updated_hosts[host->address()->asString()] = host;
  }

  const bool slot_updated =
      lb_factory_ ? lb_factory_->onClusterSlotUpdate(std::move(slots), updated_hosts) : false;

  // If slot is updated, call updateAllHosts regardless of if there's new hosts to force
  // update of the thread local load balancers.
  if (host_updated || slot_updated) {
    ASSERT(std::all_of(hosts_.begin(), hosts_.end(), [&](const auto& host) {
      return host->priority() == localityLbEndpoint().priority();
    }));
    updateAllHosts(hosts_added, hosts_removed, localityLbEndpoint().priority());
  } else {
    info_->configUpdateStats().update_no_rebuild_.inc();
  }

  // TODO(hyang): If there is an initialize callback, fire it now. Note that if the
  // cluster refers to multiple DNS names, this will return initialized after a single
  // DNS resolution completes. This is not perfect but is easier to code and it is unclear
  // if the extra complexity is needed so will start with this.
  onPreInitComplete();
}

void RedisCluster::reloadHealthyHostsHelper(const Upstream::HostSharedPtr& host) {
  if (lb_factory_) {
    lb_factory_->onHostHealthUpdate();
  }
  if (host && (host->coarseHealth() == Upstream::Host::Health::Degraded ||
               host->coarseHealth() == Upstream::Host::Health::Unhealthy)) {
    refresh_manager_->onHostDegraded(cluster_name_);
  }
  ClusterImplBase::reloadHealthyHostsHelper(host);
}

// DnsDiscoveryResolveTarget
RedisCluster::DnsDiscoveryResolveTarget::DnsDiscoveryResolveTarget(RedisCluster& parent,
                                                                   const std::string& dns_address,
                                                                   const uint32_t port)
    : parent_(parent), dns_address_(dns_address), port_(port) {}

RedisCluster::DnsDiscoveryResolveTarget::~DnsDiscoveryResolveTarget() {
  if (active_query_) {
    active_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
  // Disable timer for mock tests.
  if (resolve_timer_) {
    resolve_timer_->disableTimer();
  }
}

void RedisCluster::DnsDiscoveryResolveTarget::startResolveDns() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);

  active_query_ = parent_.dns_resolver_->resolve(
      dns_address_, parent_.dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<Network::DnsResponse>&& response) -> void {
        active_query_ = nullptr;
        ENVOY_LOG(trace, "async DNS resolution complete for {}", dns_address_);
        if (status == Network::DnsResolver::ResolutionStatus::Failure || response.empty()) {
          if (status == Network::DnsResolver::ResolutionStatus::Failure) {
            parent_.info_->configUpdateStats().update_failure_.inc();
          } else {
            parent_.info_->configUpdateStats().update_empty_.inc();
          }

          if (!resolve_timer_) {
            resolve_timer_ =
                parent_.dispatcher_.createTimer([this]() -> void { startResolveDns(); });
          }
          // if the initial dns resolved to empty, we'll skip the redis discovery phase and
          // treat it as an empty cluster.
          parent_.onPreInitComplete();
          resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
        } else {
          // Once the DNS resolve the initial set of addresses, call startResolveRedis on
          // the RedisDiscoverySession. The RedisDiscoverySession will using the "cluster
          // slots" command for service discovery and slot allocation. All subsequent
          // discoveries are handled by RedisDiscoverySession and will not use DNS
          // resolution again.
          parent_.redis_discovery_session_->registerDiscoveryAddress(std::move(response), port_);
          parent_.redis_discovery_session_->startResolveRedis();
        }
      });
}

// RedisCluster
RedisCluster::RedisDiscoverySession::RedisDiscoverySession(
    Envoy::Extensions::Clusters::Redis::RedisCluster& parent,
    NetworkFilters::Common::Redis::Client::ClientFactory& client_factory)
    : parent_(parent), dispatcher_(parent.dispatcher_),
      resolve_timer_(parent.dispatcher_.createTimer([this]() -> void { startResolveRedis(); })),
      client_factory_(client_factory), buffer_timeout_(0),
      redis_command_stats_(
          NetworkFilters::Common::Redis::RedisCommandStats::createRedisCommandStats(
              parent_.info()->statsScope().symbolTable())) {}

// Convert the cluster slot IP/Port response to an address, return null if the response
// does not match the expected type.
Network::Address::InstanceConstSharedPtr
RedisCluster::RedisDiscoverySession::RedisDiscoverySession::ipAddressFromClusterEntry(
    const std::vector<NetworkFilters::Common::Redis::RespValue>& array) {
  return Network::Utility::parseInternetAddressNoThrow(array[0].asString(), array[1].asInteger(),
                                                       false);
}

RedisCluster::RedisDiscoverySession::~RedisDiscoverySession() {
  if (current_request_) {
    current_request_->cancel();
    current_request_ = nullptr;
  }
  // Disable timer for mock tests.
  if (resolve_timer_) {
    resolve_timer_->disableTimer();
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
    std::list<Envoy::Network::DnsResponse>&& response, const uint32_t port) {
  // Since the address from DNS does not have port, we need to make a new address that has
  // port in it.
  for (const Network::DnsResponse& res : response) {
    const auto& addrinfo = res.addrInfo();
    ASSERT(addrinfo.address_ != nullptr);
    discovery_address_list_.push_back(
        Network::Utility::getAddressWithPort(*(addrinfo.address_), port));
  }
}

void RedisCluster::RedisDiscoverySession::startResolveRedis() {
  parent_.info_->configUpdateStats().update_attempt_.inc();
  // If a resolution is currently in progress, skip it.
  if (current_request_) {
    ENVOY_LOG(debug, "redis cluster slot request is already in progress for '{}'",
              parent_.info_->name());
    return;
  }

  // If hosts is empty, we haven't received a successful result from the CLUSTER SLOTS call
  // yet. So, pick a random discovery address from dns and make a request.
  Upstream::HostSharedPtr host;
  if (parent_.hosts_.empty()) {
    const int rand_idx = parent_.random_.random() % discovery_address_list_.size();
    auto it = std::next(discovery_address_list_.begin(), rand_idx);
    host = Upstream::HostSharedPtr{
        new RedisHost(parent_.info(), "", *it, parent_, true, parent_.timeSource())};
  } else {
    const int rand_idx = parent_.random_.random() % parent_.hosts_.size();
    host = parent_.hosts_[rand_idx];
  }

  current_host_address_ = host->address()->asString();
  RedisDiscoveryClientPtr& client = client_map_[current_host_address_];
  if (!client) {
    client = std::make_unique<RedisDiscoveryClient>(*this);
    client->host_ = current_host_address_;
    client->client_ = client_factory_.create(host, dispatcher_, shared_from_this(),
                                             redis_command_stats_, parent_.info()->statsScope(),
                                             parent_.auth_username_, parent_.auth_password_, false);
    client->client_->addConnectionCallbacks(*client);
  }
  ENVOY_LOG(debug, "executing redis cluster slot request for '{}'", parent_.info_->name());
  current_request_ = client->client_->makeRequest(ClusterSlotsRequest::instance_, *this);
}

void RedisCluster::RedisDiscoverySession::updateDnsStats(
    Network::DnsResolver::ResolutionStatus status, bool empty_response) {
  if (status == Network::DnsResolver::ResolutionStatus::Failure) {
    parent_.info_->configUpdateStats().update_failure_.inc();
  } else if (empty_response) {
    parent_.info_->configUpdateStats().update_empty_.inc();
  }
}

/**
 * Resolve the primary cluster entry hostname in each slot.
 * If the primary is successfully resolved, we proceed to resolve replicas.
 * We use the count of hostnames that require resolution to decide when the resolution process is
 * completed, and then call the post-resolution hooks.
 *
 * If resolving any one of the primary replicas fails, we stop the resolution process and reset
 * the timers to retry the resolution. Failure to resolve a replica, on the other hand does not
 * stop the process. If we replica resolution fails, we simply log a warning, and move to resolving
 * the rest.
 *
 * @param slots the list of slots which may need DNS resolution
 * @param address_resolution_required_cnt the number of hostnames that need DNS resolution
 */
void RedisCluster::RedisDiscoverySession::resolveClusterHostnames(
    ClusterSlotsSharedPtr&& slots,
    std::shared_ptr<std::uint64_t> hostname_resolution_required_cnt) {
  for (uint64_t slot_idx = 0; slot_idx < slots->size(); slot_idx++) {
    auto& slot = (*slots)[slot_idx];
    if (slot.primary() == nullptr) {
      ENVOY_LOG(debug,
                "starting async DNS resolution for primary slot address {} at index location {}",
                slot.primary_hostname_, slot_idx);
      parent_.dns_resolver_->resolve(
          slot.primary_hostname_, parent_.dns_lookup_family_,
          [this, slot_idx, slots, hostname_resolution_required_cnt](
              Network::DnsResolver::ResolutionStatus status, absl::string_view,
              std::list<Network::DnsResponse>&& response) -> void {
            auto& slot = (*slots)[slot_idx];
            ENVOY_LOG(
                debug,
                "async DNS resolution complete for primary slot address {} at index location {}",
                slot.primary_hostname_, slot_idx);
            updateDnsStats(status, response.empty());
            // If DNS resolution for a primary fails, we stop resolution for remaining, and reset
            // the timer.
            if (status != Network::DnsResolver::ResolutionStatus::Completed) {
              ENVOY_LOG(error, "Unable to resolve cluster slot primary hostname {}",
                        slot.primary_hostname_);
              resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
              return;
            }
            // Primary slot address resolved
            slot.setPrimary(Network::Utility::getAddressWithPort(
                *response.front().addrInfo().address_, slot.primary_port_));
            (*hostname_resolution_required_cnt)--;
            // Continue on to resolve replicas
            resolveReplicas(slots, slot_idx, hostname_resolution_required_cnt);
          });
    } else {
      resolveReplicas(slots, slot_idx, hostname_resolution_required_cnt);
    }
  }
}

/**
 * Resolve the replicas in a cluster entry. If there are no replicas, simply return.
 * If all the hostnames have been resolved, call post-resolution methods.
 * Failure to resolve a replica does not stop the overall resolution process. We log a
 * warning, and move to the next one.
 *
 * @param slots the list of slots which may need DNS resolution
 * @param index the specific index into `slots` whose replicas need to be resolved
 * @param address_resolution_required_cnt the number of address that need to be resolved
 */
void RedisCluster::RedisDiscoverySession::resolveReplicas(
    ClusterSlotsSharedPtr slots, std::size_t index,
    std::shared_ptr<std::uint64_t> hostname_resolution_required_cnt) {
  auto& slot = (*slots)[index];
  if (slot.replicas_to_resolve_.empty()) {
    if (*hostname_resolution_required_cnt == 0) {
      finishClusterHostnameResolution(slots);
    }
    return;
  }

  for (uint64_t replica_idx = 0; replica_idx < slot.replicas_to_resolve_.size(); replica_idx++) {
    auto replica = slot.replicas_to_resolve_[replica_idx];
    ENVOY_LOG(debug, "starting async DNS resolution for replica address {}", replica.first);
    parent_.dns_resolver_->resolve(
        replica.first, parent_.dns_lookup_family_,
        [this, index, slots, replica_idx, hostname_resolution_required_cnt](
            Network::DnsResolver::ResolutionStatus status, absl::string_view,
            std::list<Network::DnsResponse>&& response) -> void {
          auto& slot = (*slots)[index];
          auto& replica = slot.replicas_to_resolve_[replica_idx];
          ENVOY_LOG(debug, "async DNS resolution complete for replica address {}", replica.first);
          updateDnsStats(status, response.empty());
          // If DNS resolution fails here, we move on to resolve other replicas in the list.
          // We log a warn message.
          if (status != Network::DnsResolver::ResolutionStatus::Completed) {
            ENVOY_LOG(warn, "Unable to resolve cluster replica address {}", replica.first);
          } else {
            // Replica resolved
            slot.addReplica(Network::Utility::getAddressWithPort(
                *response.front().addrInfo().address_, replica.second));
          }
          (*hostname_resolution_required_cnt)--;
          // finish resolution if all the addresses have been resolved.
          if (*hostname_resolution_required_cnt <= 0) {
            finishClusterHostnameResolution(slots);
          }
        });
  }
}

void RedisCluster::RedisDiscoverySession::finishClusterHostnameResolution(
    ClusterSlotsSharedPtr slots) {
  parent_.onClusterSlotUpdate(std::move(slots));
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

void RedisCluster::RedisDiscoverySession::onResponse(
    NetworkFilters::Common::Redis::RespValuePtr&& value) {
  ENVOY_LOG(debug, "redis cluster slot request for '{}' succeeded", parent_.info_->name());
  current_request_ = nullptr;

  const uint32_t SlotRangeStart = 0;
  const uint32_t SlotRangeEnd = 1;
  const uint32_t SlotPrimary = 2;
  const uint32_t SlotReplicaStart = 3;

  // Do nothing if the cluster is empty.
  if (value->type() != NetworkFilters::Common::Redis::RespType::Array || value->asArray().empty()) {
    onUnexpectedResponse(value);
    return;
  }

  auto cluster_slots = std::make_shared<std::vector<ClusterSlot>>();

  // https://redis.io/commands/cluster-slots
  // CLUSTER SLOTS represents nested array of redis instances, like this:
  //
  // 1) 1) (integer) 0                                      <-- start slot range
  //    2) (integer) 5460                                   <-- end slot range
  //
  //    3) 1) "127.0.0.1"                                   <-- primary slot IP ADDR(HOSTNAME)
  //       2) (integer) 30001                               <-- primary slot PORT
  //       3) "09dbe9720cda62f7865eabc5fd8857c5d2678366"
  //
  //    4) 1) "127.0.0.2"                                   <-- replica slot IP ADDR(HOSTNAME)
  //       2) (integer) 30004                               <-- replica slot PORT
  //       3) "821d8ca00d7ccf931ed3ffc7e3db0599d2271abf"
  //
  // Loop through the cluster slot response and error checks for each field.
  auto hostname_resolution_required_cnt = std::make_shared<std::uint64_t>(0);
  for (const NetworkFilters::Common::Redis::RespValue& part : value->asArray()) {
    if (part.type() != NetworkFilters::Common::Redis::RespType::Array) {
      onUnexpectedResponse(value);
      return;
    }

    // Row 1-2: Slot ranges
    const std::vector<NetworkFilters::Common::Redis::RespValue>& slot_range = part.asArray();
    if (slot_range.size() < 3 ||
        slot_range[SlotRangeStart].type() !=
            NetworkFilters::Common::Redis::RespType::Integer || // Start slot range is an
                                                                // integer.
        slot_range[SlotRangeEnd].type() !=
            NetworkFilters::Common::Redis::RespType::Integer) { // End slot range is an
                                                                // integer.
      onUnexpectedResponse(value);
      return;
    }

    // Row 3: Primary slot address
    if (!validateCluster(slot_range[SlotPrimary])) {
      onUnexpectedResponse(value);
      return;
    }
    // Try to parse primary slot address as IP address
    // It may fail in case the address is a hostname. If this is the case - we'll come back later
    // and try to resolve hostnames asynchronously. For example, AWS ElastiCache returns hostname
    // instead of IP address.
    ClusterSlot slot(slot_range[SlotRangeStart].asInteger(), slot_range[SlotRangeEnd].asInteger(),
                     ipAddressFromClusterEntry(slot_range[SlotPrimary].asArray()));
    if (slot.primary() == nullptr) {
      // Primary address is potentially a hostname, save it for async DNS resolution.
      const auto& array = slot_range[SlotPrimary].asArray();
      slot.primary_hostname_ = array[0].asString();
      slot.primary_port_ = array[1].asInteger();
      (*hostname_resolution_required_cnt)++;
    }

    // Row 4-N: Replica(s) addresses
    for (auto replica = std::next(slot_range.begin(), SlotReplicaStart);
         replica != slot_range.end(); ++replica) {
      if (!validateCluster(*replica)) {
        onUnexpectedResponse(value);
        return;
      }
      auto replica_address = ipAddressFromClusterEntry(replica->asArray());
      if (replica_address) {
        slot.addReplica(std::move(replica_address));
      } else {
        // Replica address is potentially a hostname, save it for async DNS resolution.
        const auto& array = replica->asArray();
        slot.addReplicaToResolve(array[0].asString(), array[1].asInteger());
        (*hostname_resolution_required_cnt)++;
      }
    }
    cluster_slots->push_back(std::move(slot));
  }

  if (*hostname_resolution_required_cnt > 0) {
    // DNS resolution is required, defer finalizing the slot update until resolution is complete.
    resolveClusterHostnames(std::move(cluster_slots), hostname_resolution_required_cnt);
  } else {
    // All slots addresses were represented by IP/Port pairs.
    parent_.onClusterSlotUpdate(std::move(cluster_slots));
    resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
  }
}

// Ensure that Slot Cluster response has valid format
bool RedisCluster::RedisDiscoverySession::validateCluster(
    const NetworkFilters::Common::Redis::RespValue& value) {
  // Verify data types
  if (value.type() != NetworkFilters::Common::Redis::RespType::Array) {
    return false;
  }
  const auto& array = value.asArray();
  if (array.size() < 2 || array[0].type() != NetworkFilters::Common::Redis::RespType::BulkString ||
      array[1].type() != NetworkFilters::Common::Redis::RespType::Integer) {
    return false;
  }
  // Verify IP/Host address
  if (array[0].asString().empty()) {
    return false;
  }
  // Verify port
  if (array[1].asInteger() > 0xffff) {
    return false;
  }

  return true;
}

void RedisCluster::RedisDiscoverySession::onUnexpectedResponse(
    const NetworkFilters::Common::Redis::RespValuePtr& value) {
  ENVOY_LOG(warn, "Unexpected response to cluster slot command: {}", value->toString());
  this->parent_.info_->configUpdateStats().update_failure_.inc();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

void RedisCluster::RedisDiscoverySession::onFailure() {
  ENVOY_LOG(debug, "redis cluster slot request for '{}' failed", parent_.info_->name());
  current_request_ = nullptr;
  if (!current_host_address_.empty()) {
    auto client_to_delete = client_map_.find(current_host_address_);
    client_to_delete->second->client_->close();
  }
  parent_.info()->configUpdateStats().update_failure_.inc();
  resolve_timer_->enableTimer(parent_.cluster_refresh_rate_);
}

RedisCluster::ClusterSlotsRequest RedisCluster::ClusterSlotsRequest::instance_;

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
RedisClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::redis::v3::RedisClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  if (!cluster.has_cluster_type() || cluster.cluster_type().name() != "envoy.clusters.redis") {
    return absl::InvalidArgumentError("Redis cluster can only created with redis cluster type.");
  }
  auto resolver =
      THROW_OR_RETURN_VALUE(selectDnsResolver(cluster, context), Network::DnsResolverSharedPtr);
  // TODO(hyang): This is needed to migrate existing cluster, disallow using other lb_policy
  // in the future
  absl::Status creation_status = absl::OkStatus();
  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    auto ret =
        std::make_pair(std::shared_ptr<RedisCluster>(new RedisCluster(
                           cluster, proto_config, context,
                           NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_,
                           resolver, nullptr, creation_status)),
                       nullptr);
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }
  auto lb_factory = std::make_shared<RedisClusterLoadBalancerFactory>(
      context.serverFactoryContext().api().randomGenerator());
  absl::StatusOr<std::unique_ptr<RedisCluster>> cluster_or_error = RedisCluster::create(
      cluster, proto_config, context,
      NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_, resolver, lb_factory);
  RETURN_IF_NOT_OK(cluster_or_error.status());
  return std::make_pair(std::shared_ptr<RedisCluster>(std::move(*cluster_or_error)),
                        std::make_unique<RedisClusterThreadAwareLoadBalancer>(lb_factory));
}

REGISTER_FACTORY(RedisClusterFactory, Upstream::ClusterFactory);

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
