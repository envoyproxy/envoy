#include "source/extensions/clusters/eds/eds.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Upstream {

namespace {

// Returns whether a host can serve traffic without being excluded from load balancing, i.e. whether
// it is healthy or degraded and not excluded.
bool isHostServingTraffic(const Host& host) {
  return !host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC) &&
         !host.healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK) &&
         !host.healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH) &&
         !host.healthFlagGet(Host::HealthFlag::EDS_STATUS_DRAINING) &&
         !host.healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC) &&
         !host.healthFlagGet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);
}

// Returns whether a host is strictly healthy (neither unhealthy nor degraded) and not excluded from
// load balancing.
bool isHostHealthy(const Host& host) {
  return isHostServingTraffic(host) && !host.healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC) &&
         !host.healthFlagGet(Host::HealthFlag::DEGRADED_OUTLIER_DETECTION) &&
         !host.healthFlagGet(Host::HealthFlag::DEGRADED_EDS_HEALTH);
}

// With graceful host removal, returns whether a host removed by service discovery can be retained
// with PENDING_DYNAMIC_REMOVAL, i.e. whether it is actively health checked and healthy.
bool canRetainHost(const Host& host) {
  return !host.disableActiveHealthCheck() && isHostHealthy(host);
}

// With graceful host removal, returns whether a host is warming up, i.e. whether it is awaiting its
// initial active health check and will be able to serve traffic (healthy or degraded) once it
// passes it.
bool isHostWarmingUp(const Host& host) {
  return host.healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC) &&
         !host.disableActiveHealthCheck() &&
         !host.healthFlagGet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL) &&
         !host.healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK) &&
         !host.healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH) &&
         !host.healthFlagGet(Host::HealthFlag::EDS_STATUS_DRAINING);
}

struct PriorityWarmupState {
  bool has_warming_host{};
  bool meets_panic_threshold{};
};

bool isHostExcludedFromPanicThreshold(const Host& host) {
  return host.healthFlagGet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL) ||
         host.healthFlagGet(Host::HealthFlag::EDS_STATUS_DRAINING);
}

// Evaluates whether a priority has warming hosts and whether its healthy non-retained hosts meet
// the cluster's healthy panic threshold. Including warming (PENDING_ACTIVE_HC) hosts in
// eligible_hosts ensures that dropping retained hosts never leaves the priority below the load
// balancer's healthy panic threshold even if all remaining warming hosts fail their initial active
// health check.
PriorityWarmupState evaluatePriorityWarmupState(const HostVector& hosts,
                                                uint64_t panic_threshold_percent) {
  PriorityWarmupState state;
  size_t eligible_hosts = 0;
  size_t healthy_hosts = 0;
  for (const HostSharedPtr& host : hosts) {
    if (host->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL) ||
        isHostExcludedFromPanicThreshold(*host)) {
      continue;
    }
    if (isHostWarmingUp(*host)) {
      state.has_warming_host = true;
    }
    ++eligible_hosts;
    if (isHostHealthy(*host)) {
      ++healthy_hosts;
    }
  }
  state.meets_panic_threshold = healthy_hosts > 0 && uint64_t{100} * healthy_hosts >=
                                                         panic_threshold_percent * eligible_hosts;
  return state;
}

} // namespace

absl::StatusOr<std::unique_ptr<EdsClusterImpl>>
EdsClusterImpl::create(const envoy::config::cluster::v3::Cluster& cluster,
                       ClusterFactoryContext& cluster_context) {
  absl::Status creation_status = absl::OkStatus();
  std::unique_ptr<EdsClusterImpl> ret =
      absl::WrapUnique(new EdsClusterImpl(cluster, cluster_context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

EdsClusterImpl::EdsClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                               ClusterFactoryContext& cluster_context,
                               absl::Status& creation_status)
    : BaseDynamicClusterImpl(cluster, cluster_context, creation_status),
      resource_type_helper_(cluster_context.messageValidationVisitor(), "cluster_name"),
      local_info_(cluster_context.serverFactoryContext().localInfo()),
      main_thread_dispatcher_(cluster_context.serverFactoryContext().mainThreadDispatcher()),
      eds_resources_cache_(
          cluster_context.serverFactoryContext().clusterManager().edsResourcesCache()) {
  RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  if (info_->lbConfig().has_healthy_panic_threshold()) {
    const double panic_percent = info_->lbConfig().healthy_panic_threshold().value();
    if (std::isnan(panic_percent) || panic_percent < 0.0 || panic_percent > 100.0) {
      creation_status = absl::InvalidArgumentError("Value not in the range of 0..100 range.");
      return;
    }
  }
  default_healthy_panic_percent_ = PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
      info_->lbConfig(), healthy_panic_threshold, 100, 50);
  Event::Dispatcher& dispatcher = cluster_context.serverFactoryContext().mainThreadDispatcher();
  assignment_timeout_ = dispatcher.createTimer([this]() -> void { onAssignmentTimeout(); });
  const auto& eds_config = cluster.eds_cluster_config().eds_config();
  if (Config::SubscriptionFactory::isPathBasedConfigSource(
          eds_config.config_source_specifier_case())) {
    initialize_phase_ = InitializePhase::Primary;
  } else {
    initialize_phase_ = InitializePhase::Secondary;
  }
  const auto resource_name = resource_type_helper_.getResourceName();
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.xdstp_based_config_singleton_subscriptions")) {
    subscription_ = THROW_OR_RETURN_VALUE(
        cluster_context.serverFactoryContext().xdsManager().subscribeToSingletonResource(
            edsServiceName(), eds_config, Grpc::Common::typeUrl(resource_name), info_->statsScope(),
            *this, resource_type_helper_.resourceDecoder(), {}),
        Config::SubscriptionPtr);
  } else {
    subscription_ = THROW_OR_RETURN_VALUE(
        cluster_context.serverFactoryContext()
            .clusterManager()
            .subscriptionFactory()
            .subscriptionFromConfigSource(eds_config, Grpc::Common::typeUrl(resource_name),
                                          info_->statsScope(), *this,
                                          resource_type_helper_.resourceDecoder(), {}),
        Config::SubscriptionPtr);
  }
}

EdsClusterImpl::~EdsClusterImpl() {
  if (using_cached_resource_) {
    // Clear the callback as the subscription is no longer valid.
    eds_resources_cache_->removeCallback(edsServiceName(), this);
  }
}

void EdsClusterImpl::startPreInit() { subscription_->start({edsServiceName()}); }

void EdsClusterImpl::BatchUpdateHelper::batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) {
  absl::flat_hash_set<std::string> all_new_hosts;
  PriorityStateManager priority_state_manager(parent_, parent_.local_info_, &host_update_cb);
  for (const auto& locality_lb_endpoint : cluster_load_assignment_.endpoints()) {
    priority_state_manager.initializePriorityFor(locality_lb_endpoint);

    if (locality_lb_endpoint.has_leds_cluster_locality_config()) {
      // The locality uses LEDS, fetch its dynamic data, which must be ready, or otherwise
      // the batchUpdate method should not have been called.
      const auto& leds_config = locality_lb_endpoint.leds_cluster_locality_config();

      // The batchUpdate call must be performed after all the endpoints of all localities
      // were received.
      const auto it = parent_.leds_localities_.find(leds_config);
      ASSERT(it != parent_.leds_localities_.end() && it->second->isUpdated());
      for (const auto& [_, lb_endpoint] : it->second->getEndpointsMap()) {
        updateLocalityEndpoints(lb_endpoint, locality_lb_endpoint, priority_state_manager,
                                all_new_hosts);
      }
    } else {
      for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
        updateLocalityEndpoints(lb_endpoint, locality_lb_endpoint, priority_state_manager,
                                all_new_hosts);
      }
    }
  }

  // Track whether we rebuilt any LB structures.
  bool cluster_rebuilt = false;

  // Get the map of all the latest existing hosts, which is used to filter out the existing
  // hosts in the process of updating cluster memberships.
  HostLookupMapConstSharedPtr all_hosts = parent_.prioritySet().crossPriorityHostMap();
  ASSERT(all_hosts != nullptr);

  const uint32_t overprovisioning_factor = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      cluster_load_assignment_.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);
  const bool weighted_priority_health =
      cluster_load_assignment_.policy().weighted_priority_health();

  LocalityWeightsMap empty_locality_map;

  // Loop over all priorities that exist in the new configuration.
  auto& priority_state = priority_state_manager.priorityState();
  THROW_IF_NOT_OK(parent_.validateEndpoints(cluster_load_assignment_.endpoints(), priority_state));
  if (parent_.info()->gracefulHostRemoval()) {
    // With graceful host removal, whether removed hosts are retained depends on the hosts of all
    // the priorities, which are therefore all reconciled with the configuration before any of them
    // is updated.
    if (!parent_.updatePrioritiesWithGracefulHostRemoval(
            weighted_priority_health, overprovisioning_factor, priority_state_manager, *all_hosts,
            all_new_hosts)) {
      parent_.info_->configUpdateStats().update_no_rebuild_.inc();
    }
    parent_.onPreInitComplete();
    return;
  }
  for (size_t i = 0; i < priority_state.size(); ++i) {
    if (parent_.locality_weights_map_.size() <= i) {
      parent_.locality_weights_map_.resize(i + 1);
    }
    if (priority_state[i].first != nullptr) {
      cluster_rebuilt |= parent_.updateHostsPerLocality(
          i, weighted_priority_health, overprovisioning_factor, *priority_state[i].first,
          parent_.locality_weights_map_[i], priority_state[i].second, priority_state_manager,
          *all_hosts, all_new_hosts);
    } else {
      // If the new update contains a priority with no hosts, call the update function with an empty
      // set of hosts.
      cluster_rebuilt |=
          parent_.updateHostsPerLocality(i, weighted_priority_health, overprovisioning_factor, {},
                                         parent_.locality_weights_map_[i], empty_locality_map,
                                         priority_state_manager, *all_hosts, all_new_hosts);
    }
  }

  // Loop over all priorities not present in the config that already exists. This will
  // empty out any remaining priority that the config update did not refer to.
  for (size_t i = priority_state.size(); i < parent_.priority_set_.hostSetsPerPriority().size();
       ++i) {
    if (parent_.locality_weights_map_.size() <= i) {
      parent_.locality_weights_map_.resize(i + 1);
    }
    cluster_rebuilt |= parent_.updateHostsPerLocality(
        i, weighted_priority_health, overprovisioning_factor, {}, parent_.locality_weights_map_[i],
        empty_locality_map, priority_state_manager, *all_hosts, all_new_hosts);
  }

  if (!cluster_rebuilt) {
    parent_.info_->configUpdateStats().update_no_rebuild_.inc();
  }

  // If we didn't setup to initialize when our first round of health checking is complete, just
  // do it now.
  parent_.onPreInitComplete();
}

void EdsClusterImpl::BatchUpdateHelper::updateLocalityEndpoints(
    const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint,
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
    PriorityStateManager& priority_state_manager, absl::flat_hash_set<std::string>& all_new_hosts) {
  const auto address =
      THROW_OR_RETURN_VALUE(parent_.resolveProtoAddress(lb_endpoint.endpoint().address()),
                            const Network::Address::InstanceConstSharedPtr);
  std::vector<Network::Address::InstanceConstSharedPtr> address_list;
  if (!lb_endpoint.endpoint().additional_addresses().empty()) {
    address_list.push_back(address);
    for (const auto& additional_address : lb_endpoint.endpoint().additional_addresses()) {
      Network::Address::InstanceConstSharedPtr address =
          returnOrThrow(parent_.resolveProtoAddress(additional_address.address()));
      address_list.emplace_back(address);
    }
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.happy_eyeballs_sort_non_ip_addresses")) {
      for (const Network::Address::InstanceConstSharedPtr& address : address_list) {
        // All addresses must by IP addresses.
        if (!address->ip()) {
          throwEnvoyExceptionOrPanic("additional_addresses must be IP addresses.");
        }
      }
    }
  }

  // When the configuration contains duplicate hosts, only the first one will be retained.
  const auto address_as_string = address->asString();
  if (all_new_hosts.contains(address_as_string)) {
    return;
  }

  priority_state_manager.registerHostForPriority(lb_endpoint.endpoint().hostname(), address,
                                                 address_list, locality_lb_endpoint, lb_endpoint);
  all_new_hosts.emplace(address_as_string);
}

absl::Status
EdsClusterImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                               const std::string&) {
  if (resources.empty()) {
    ENVOY_LOG(debug, "Missing ClusterLoadAssignment for {} in onConfigUpdate()", edsServiceName());
    info_->configUpdateStats().update_empty_.inc();
    onPreInitComplete();
    return absl::OkStatus();
  }
  if (resources.size() != 1) {
    const auto msg = fmt::format("Unexpected EDS resource length: {}", resources.size());
    ENVOY_LOG(warn, "eds: cluster '{}' config rejected: {}", edsServiceName(), msg);
    return absl::InvalidArgumentError(msg);
  }

  const auto& cluster_load_assignment =
      Envoy::Protobuf::DynamicCastMessage<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          resources[0].get().resource());
  if (cluster_load_assignment.cluster_name() != edsServiceName()) {
    const auto msg = fmt::format("Unexpected EDS cluster (expecting {}): {}", edsServiceName(),
                                 cluster_load_assignment.cluster_name());
    ENVOY_LOG(warn, "eds: cluster '{}' config rejected: {}", edsServiceName(), msg);
    return absl::InvalidArgumentError(msg);
  }
  // Validate that each locality doesn't have both LEDS and endpoints defined.
  // TODO(adisuissa): This is only needed for the API v3 support. In future major versions
  // the oneof definition will take care of it.
  for (const auto& locality : cluster_load_assignment.endpoints()) {
    if (locality.has_leds_cluster_locality_config() && locality.lb_endpoints_size() > 0) {
      const auto msg = fmt::format(
          "A ClusterLoadAssignment for cluster {} cannot include both LEDS (resource: {}) and a "
          "list of endpoints.",
          edsServiceName(), locality.leds_cluster_locality_config().leds_collection_name());
      ENVOY_LOG(warn, "eds: cluster '{}' config rejected: {}", edsServiceName(), msg);
      return absl::InvalidArgumentError(msg);
    }
  }

  // Disable timer (if enabled) as we have received new assignment.
  if (assignment_timeout_->enabled()) {
    assignment_timeout_->disableTimer();
    if (eds_resources_cache_.has_value()) {
      eds_resources_cache_->disableExpiryTimer(edsServiceName());
    }
  }
  // Check if endpoint_stale_after is set.
  const uint64_t stale_after_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(cluster_load_assignment.policy(), endpoint_stale_after, 0);
  if (stale_after_ms > 0) {
    // Stat to track how often we receive valid assignment_timeout in response.
    info_->configUpdateStats().assignment_timeout_received_.inc();
    assignment_timeout_->enableTimer(std::chrono::milliseconds(stale_after_ms));
    if (eds_resources_cache_.has_value()) {
      eds_resources_cache_->setExpiryTimer(edsServiceName(),
                                           std::chrono::milliseconds(stale_after_ms));
    }
  }

  update(cluster_load_assignment);
  // If previously used a cached version, remove the subscription from the cache's
  // callbacks.
  if (using_cached_resource_) {
    eds_resources_cache_->removeCallback(edsServiceName(), this);
    using_cached_resource_ = false;
  }
  return absl::OkStatus();
}

void EdsClusterImpl::update(
    const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
  // Drop overload configuration parsing.
  THROW_IF_NOT_OK(parseDropOverloadConfig(cluster_load_assignment));

  // Compare the current set of LEDS localities (localities using LEDS) to the one received in the
  // update. A LEDS locality can either be added, removed, or kept. If it is added we add a
  // subscription to it, and if it is removed we delete the subscription.
  LedsConfigSet cla_leds_configs;

  for (const auto& locality : cluster_load_assignment.endpoints()) {
    if (locality.has_leds_cluster_locality_config()) {
      cla_leds_configs.emplace(locality.leds_cluster_locality_config());
    }
  }

  // Remove the LEDS localities that are not needed anymore.
  absl::erase_if(leds_localities_, [&cla_leds_configs](const auto& item) {
    auto const& [leds_config, _] = item;
    // Returns true if the leds_config isn't in the cla_leds_configs
    return cla_leds_configs.find(leds_config) == cla_leds_configs.end();
  });

  const envoy::config::endpoint::v3::ClusterLoadAssignment* used_load_assignment;
  if (!cla_leds_configs.empty() || eds_resources_cache_.has_value()) {
    cluster_load_assignment_ =
        ArenaWrappedProto<envoy::config::endpoint::v3::ClusterLoadAssignment>(
            cluster_load_assignment);
    used_load_assignment = cluster_load_assignment_.get();
  } else {
    cluster_load_assignment_ = nullptr;
    used_load_assignment = &cluster_load_assignment;
  }

  // Add all the LEDS localities that are new.
  for (const auto& leds_config : cla_leds_configs) {
    if (leds_localities_.find(leds_config) == leds_localities_.end()) {
      ENVOY_LOG(trace, "Found new LEDS config in EDS onConfigUpdate() for cluster {}: {}",
                edsServiceName(), leds_config.DebugString());

      // Create a new LEDS subscription and add it to the subscriptions map.
      LedsSubscriptionPtr leds_locality_subscription = std::make_unique<LedsSubscription>(
          leds_config, edsServiceName(), *transport_factory_context_, info_->statsScope(),
          [this]() {
            if (validateAllLedsUpdated()) {
              ASSERT(cluster_load_assignment_ != nullptr);
              BatchUpdateHelper helper(*this, *cluster_load_assignment_);
              priority_set_.batchHostUpdate(helper);
            }
          });
      leds_localities_.emplace(leds_config, std::move(leds_locality_subscription));
    }
  }

  // If all the LEDS localities are updated, the EDS update can occur. If not, then when the last
  // LEDS locality will be updated, it will trigger the EDS update helper.
  if (!validateAllLedsUpdated()) {
    return;
  }

  BatchUpdateHelper helper(*this, *used_load_assignment);
  priority_set_.batchHostUpdate(helper);
}

absl::Status
EdsClusterImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                               const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
  return onConfigUpdate(added_resources, "");
}

void EdsClusterImpl::onAssignmentTimeout() {
  // We can no longer use the assignments, remove them.
  // TODO(vishalpowar) This is not going to work for incremental updates, and we
  // need to instead change the health status to indicate the assignments are
  // stale.
  // TODO(snowp): This should probably just use xDS TTLs?
  envoy::config::endpoint::v3::ClusterLoadAssignment resource;
  resource.set_cluster_name(edsServiceName());
  update(std::move(resource));

  if (eds_resources_cache_.has_value()) {
    // Clear the resource so it won't be used, and its watchers will be notified.
    eds_resources_cache_->removeResource(edsServiceName());
  }
  // Stat to track how often we end up with stale assignments.
  info_->configUpdateStats().assignment_stale_.inc();
}

void EdsClusterImpl::onCachedResourceRemoved(absl::string_view resource_name) {
  ASSERT(resource_name == edsServiceName());
  // Disable the timer if previously started.
  if (assignment_timeout_->enabled()) {
    assignment_timeout_->disableTimer();
    eds_resources_cache_->disableExpiryTimer(edsServiceName());
  }
  envoy::config::endpoint::v3::ClusterLoadAssignment resource;
  resource.set_cluster_name(edsServiceName());
  update(std::move(resource));
}

void EdsClusterImpl::reloadHealthyHostsHelper(const HostSharedPtr& host) {
  if (info()->gracefulHostRemoval()) {
    reloadHealthyHostsWithGracefulHostRemoval(/*remove_hosts=*/false, host);
    return;
  }

  // Here we will see if we have a host that has been marked for deletion by service discovery
  // but has been stabilized due to passing active health checking. If such a host is now
  // failing active health checking we can remove it during this health check update.
  HostSharedPtr host_to_exclude = host;
  if (host_to_exclude != nullptr &&
      host_to_exclude->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC) &&
      host_to_exclude->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL)) {
    // Empty for clarity.
  } else {
    // Do not exclude and remove the host during the update.
    host_to_exclude = nullptr;
  }

  const auto& host_sets = prioritySet().hostSetsPerPriority();
  for (size_t priority = 0; priority < host_sets.size(); ++priority) {
    const auto& host_set = host_sets[priority];

    // Filter current hosts in case we need to exclude a host.
    HostVectorSharedPtr hosts_copy(new HostVector());
    std::copy_if(host_set->hosts().begin(), host_set->hosts().end(),
                 std::back_inserter(*hosts_copy),
                 [&host_to_exclude](const HostSharedPtr& host) { return host_to_exclude != host; });

    // Setup a hosts to remove vector in case we need to exclude a host.
    HostVector hosts_to_remove;
    if (hosts_copy->size() != host_set->hosts().size()) {
      ASSERT(hosts_copy->size() == host_set->hosts().size() - 1);
      hosts_to_remove.emplace_back(host_to_exclude);
    }

    // Filter hosts per locality in case we need to exclude a host.
    HostsPerLocalityConstSharedPtr hosts_per_locality_copy = host_set->hostsPerLocality().filter(
        {[&host_to_exclude](const Host& host) { return &host != host_to_exclude.get(); }})[0];

    prioritySet().updateHosts(
        priority, HostSetImpl::partitionHosts(hosts_copy, hosts_per_locality_copy),
        host_set->localityWeights(), {}, hosts_to_remove, std::nullopt, std::nullopt);
  }
}

void EdsClusterImpl::reloadHealthyHostsWithGracefulHostRemoval(const bool remove_hosts,
                                                               const HostSharedPtr& changed_host) {
  // The priorities must not be updated while a batch update is active (e.g. from the member update
  // callbacks run at the end of a batch update), as a batch update must cover all the updates of
  // its priorities. The healthy hosts are reloaded on a later event loop iteration instead.
  if (prioritySet().batchUpdateActive()) {
    graceful_host_removal_reload_pending_ = true;
    scheduleGracefulHostRemoval();
    return;
  }
  const bool reload_pending = std::exchange(graceful_host_removal_reload_pending_, false);

  // Hosts retained with PENDING_DYNAMIC_REMOVAL in a priority are kept while at least one host in
  // that priority is still warming up and the priority has not yet reached the healthy panic
  // threshold, and are removed once the threshold is met, warmup ends, or they are no longer
  // healthy.
  const auto& host_sets = prioritySet().hostSetsPerPriority();
  const uint64_t panic_threshold_percent = healthyPanicThresholdPercent();
  std::vector<PriorityWarmupState> warmup_state(host_sets.size());
  bool has_retained_host = false;
  for (size_t priority = 0; priority < host_sets.size(); ++priority) {
    const auto& hosts = host_sets[priority]->hosts();
    has_retained_host =
        has_retained_host || std::any_of(hosts.begin(), hosts.end(), [](const HostSharedPtr& host) {
          return host->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL);
        });
    warmup_state[priority] = evaluatePriorityWarmupState(hosts, panic_threshold_percent);
  }
  const auto should_remove_host_in_priority = [&](size_t priority, const Host& host) {
    if (!host.healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL)) {
      return false;
    }
    const bool retain_in_priority = health_checker_ != nullptr &&
                                    warmup_state[priority].has_warming_host &&
                                    !warmup_state[priority].meets_panic_threshold;
    return !retain_in_priority || !canRetainHost(host);
  };
  bool has_hosts_to_remove = false;
  if (has_retained_host) {
    for (size_t priority = 0; priority < host_sets.size() && !has_hosts_to_remove; ++priority) {
      const auto& hosts = host_sets[priority]->hosts();
      has_hosts_to_remove = std::any_of(hosts.begin(), hosts.end(), [&](const HostSharedPtr& host) {
        return should_remove_host_in_priority(priority, *host);
      });
    }
  }

  if (remove_hosts && !has_hosts_to_remove && !reload_pending) {
    // The removal of the retained hosts was scheduled, but they are not to be removed anymore
    // (e.g. since an update removed or added them back in the meantime). The healthy hosts were
    // already reloaded when the removal was scheduled, and by every update since.
    return;
  }

  if (!remove_hosts || !has_hosts_to_remove) {
    // Hosts are not removed when the healthy hosts are reloaded from the callbacks of the health
    // checker or of the outlier detector, since removing hosts updates both of them. They are
    // removed on a later event loop iteration instead, after the healthy hosts are updated here.
    if (has_hosts_to_remove) {
      scheduleGracefulHostRemoval();
    }
    const auto reload_priority = [&](size_t priority) {
      const auto& host_set = host_sets[priority];
      prioritySet().updateHosts(
          priority,
          HostSetImpl::partitionHosts(host_set->hostsPtr(), host_set->hostsPerLocalityPtr()),
          host_set->localityWeights(), {}, {}, std::nullopt, std::nullopt);
    };
    // As in ClusterImplBase::reloadHealthyHostsHelper(), a health change only reloads the priority
    // of the host, since the healthy hosts of a priority only depend on the hosts of that priority.
    // If an earlier reload was deferred during a batch update, reload all priorities since the
    // deferred reload may have applied to any priority.
    if (!reload_pending && changed_host != nullptr && changed_host->priority() < host_sets.size() &&
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.incremental_health_transition")) {
      reload_priority(changed_host->priority());
      return;
    }
    for (size_t priority = 0; priority < host_sets.size(); ++priority) {
      reload_priority(priority);
    }
    return;
  }

  // Remove the hosts from all the priorities in a single batch update.
  struct PriorityUpdate {
    uint32_t priority;
    PrioritySet::UpdateHostsParams update_hosts_params;
    LocalityWeightsConstSharedPtr locality_weights;
    HostVector hosts_removed;
  };
  std::vector<PriorityUpdate> updates;
  updates.reserve(host_sets.size());
  uint64_t max_host_weight = 1;
  for (uint32_t priority = 0; priority < host_sets.size(); ++priority) {
    const auto& host_set = host_sets[priority];
    auto hosts = std::make_shared<HostVector>();
    HostVector hosts_removed;
    for (const HostSharedPtr& host : host_set->hosts()) {
      if (should_remove_host_in_priority(priority, *host)) {
        hosts_removed.push_back(host);
        continue;
      }
      hosts->push_back(host);
      max_host_weight = std::max<uint64_t>(max_host_weight, host->weight());
    }
    HostsPerLocalityConstSharedPtr hosts_per_locality = host_set->hostsPerLocalityPtr();
    LocalityWeightsConstSharedPtr locality_weights = host_set->localityWeights();
    if (!hosts_removed.empty()) {
      ENVOY_LOG(debug, "removing {} retained hosts from priority {} of cluster {}",
                hosts_removed.size(), priority, info_->name());
      hosts_per_locality = hosts_per_locality->filter(
          {[&](const Host& host) { return !should_remove_host_in_priority(priority, host); }})[0];
      // Also remove the localities left without any host, along with their weights, so that the
      // localities of the priority are the same as if it was rebuilt from the configuration.
      const auto& buckets = hosts_per_locality->get();
      if (std::any_of(buckets.begin(), buckets.end(),
                      [](const HostVector& bucket) { return bucket.empty(); }) &&
          (locality_weights == nullptr || locality_weights->size() == buckets.size())) {
        std::vector<HostVector> non_empty_buckets;
        non_empty_buckets.reserve(buckets.size());
        LocalityWeightsSharedPtr non_empty_locality_weights;
        if (locality_weights != nullptr) {
          non_empty_locality_weights = std::make_shared<LocalityWeights>();
          non_empty_locality_weights->reserve(buckets.size());
        }
        bool has_local_locality = false;
        for (size_t i = 0; i < buckets.size(); ++i) {
          if (buckets[i].empty()) {
            continue;
          }
          if (i == 0 && hosts_per_locality->hasLocalLocality()) {
            has_local_locality = true;
          }
          non_empty_buckets.push_back(buckets[i]);
          if (non_empty_locality_weights != nullptr) {
            non_empty_locality_weights->push_back((*locality_weights)[i]);
          }
        }
        hosts_per_locality = std::make_shared<HostsPerLocalityImpl>(std::move(non_empty_buckets),
                                                                    has_local_locality);
        locality_weights = std::move(non_empty_locality_weights);
      }
    }
    updates.push_back({priority, HostSetImpl::partitionHosts(std::move(hosts), hosts_per_locality),
                       std::move(locality_weights), std::move(hosts_removed)});
  }
  info_->endpointStats().max_host_weight_.set(max_host_weight);

  class RemoveHostsBatchUpdate : public PrioritySet::BatchUpdateCb {
  public:
    explicit RemoveHostsBatchUpdate(std::vector<PriorityUpdate>& updates) : updates_(updates) {}

    // PrioritySet::BatchUpdateCb
    void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override {
      for (PriorityUpdate& update : updates_) {
        host_update_cb.updateHosts(update.priority, std::move(update.update_hosts_params),
                                   std::move(update.locality_weights), {}, update.hosts_removed,
                                   std::nullopt, std::nullopt);
      }
    }

  private:
    std::vector<PriorityUpdate>& updates_;
  };
  RemoveHostsBatchUpdate batch_update(updates);
  prioritySet().batchHostUpdate(batch_update);
}

void EdsClusterImpl::scheduleGracefulHostRemoval() {
  if (graceful_host_removal_timer_ == nullptr) {
    graceful_host_removal_timer_ = main_thread_dispatcher_.createTimer(
        [this]() -> void { reloadHealthyHostsWithGracefulHostRemoval(/*remove_hosts=*/true); });
  }
  if (!graceful_host_removal_timer_->enabled()) {
    graceful_host_removal_timer_->enableTimer(std::chrono::milliseconds(0));
  }
}

uint64_t EdsClusterImpl::healthyPanicThresholdPercent() const {
  return std::min<uint64_t>(100, runtime_.snapshot().getInteger("upstream.healthy_panic_threshold",
                                                                default_healthy_panic_percent_));
}

bool EdsClusterImpl::updateHostsPerLocality(
    const uint32_t priority, bool weighted_priority_health, const uint32_t overprovisioning_factor,
    const HostVector& new_hosts, LocalityWeightsMap& locality_weights_map,
    LocalityWeightsMap& new_locality_weights_map, PriorityStateManager& priority_state_manager,
    const HostLookupMap& all_hosts, const absl::flat_hash_set<std::string>& all_new_hosts) {
  const auto& host_set = priority_set_.getOrCreateHostSet(priority, overprovisioning_factor);
  HostVectorSharedPtr current_hosts_copy(new HostVector(host_set.hosts()));

  HostVector hosts_added;
  HostVector hosts_removed;
  hosts_added.reserve(new_hosts.size());
  hosts_removed.reserve(host_set.hosts().size());
  // We need to trigger updateHosts with the new host vectors if they have changed. We also do this
  // when the locality weight map or the overprovisioning factor. Note calling updateDynamicHostList
  // is responsible for both determining whether there was a change and to perform the actual update
  // to current_hosts_copy, so it must be called even if we know that we need to update (e.g. if the
  // overprovisioning factor changes).
  //
  // TODO(htuch): We eagerly update all the host sets here on weight changes, which may have
  // performance implications, since this has the knock on effect that we rebuild the load balancers
  // and locality scheduler. See the comment in BaseDynamicClusterImpl::updateDynamicHostList
  // about this. In the future we may need to do better here.
  const bool hosts_updated = updateDynamicHostList(
      new_hosts, *current_hosts_copy, hosts_added, hosts_removed,
      [&all_hosts](const std::string& address) { return all_hosts.findHost(address); },
      all_new_hosts);
  if (hosts_updated || host_set.weightedPriorityHealth() != weighted_priority_health ||
      host_set.overprovisioningFactor() != overprovisioning_factor ||
      locality_weights_map != new_locality_weights_map) {
    ASSERT(std::all_of(current_hosts_copy->begin(), current_hosts_copy->end(),
                       [&](const auto& host) { return host->priority() == priority; }));
    locality_weights_map = new_locality_weights_map;
    ENVOY_LOG(debug,
              "EDS hosts or locality weights changed for cluster: {} current hosts {} priority {}",
              info_->name(), host_set.hosts().size(), host_set.priority());

    priority_state_manager.updateClusterPrioritySet(
        priority, std::move(current_hosts_copy), hosts_added, hosts_removed, std::nullopt,
        weighted_priority_health, overprovisioning_factor);
    return true;
  }
  return false;
}

bool EdsClusterImpl::updatePrioritiesWithGracefulHostRemoval(
    const bool weighted_priority_health, const uint32_t overprovisioning_factor,
    PriorityStateManager& priority_state_manager, const HostLookupMap& all_hosts,
    const absl::flat_hash_set<std::string>& all_new_hosts) {
  // graceful_host_removal implies ignore_health_on_host_removal, so the hosts are first reconciled
  // with the configuration without retaining any host, and the removed hosts are retained below.
  ASSERT(info_->drainConnectionsOnHostRemoval());
  auto& priority_state = priority_state_manager.priorityState();
  // The existing priorities that are not present in the configuration are emptied out as
  // priorities without hosts.
  if (priority_state.size() < priority_set_.hostSetsPerPriority().size()) {
    priority_state.resize(priority_set_.hostSetsPerPriority().size());
  }
  if (locality_weights_map_.size() < priority_state.size()) {
    locality_weights_map_.resize(priority_state.size());
  }

  struct PriorityUpdate {
    const HostSet* host_set{};
    HostVectorSharedPtr hosts;
    HostVector hosts_added;
    HostVector hosts_removed;
    bool hosts_updated{};
    // Whether the priority had hosts retained with PENDING_DYNAMIC_REMOVAL before the update.
    bool had_retained_hosts{};
    PriorityWarmupState warmup_state;
  };
  std::vector<PriorityUpdate> updates(priority_state.size());
  const HostVector no_hosts;
  const uint64_t panic_threshold_percent = healthyPanicThresholdPercent();
  bool any_warming_host = false;
  for (uint32_t priority = 0; priority < updates.size(); ++priority) {
    PriorityUpdate& update = updates[priority];
    // The host sets of new priorities are not created here, but by updateClusterPrioritySet() below
    // (with the updated configuration), so that their creation is notified to the callbacks of the
    // priority set.
    const auto& host_sets = priority_set_.hostSetsPerPriority();
    update.host_set = priority < host_sets.size() ? host_sets[priority].get() : nullptr;
    const HostVector& current_hosts =
        update.host_set != nullptr ? update.host_set->hosts() : no_hosts;
    update.had_retained_hosts =
        std::any_of(current_hosts.begin(), current_hosts.end(), [](const HostSharedPtr& host) {
          return host->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL);
        });
    const HostVector& new_hosts =
        priority_state[priority].first != nullptr ? *priority_state[priority].first : no_hosts;
    update.hosts = std::make_shared<HostVector>(current_hosts);
    update.hosts_added.reserve(new_hosts.size());
    update.hosts_removed.reserve(current_hosts.size());
    update.hosts_updated = updateDynamicHostList(
        new_hosts, *update.hosts, update.hosts_added, update.hosts_removed,
        [&all_hosts](const std::string& address) { return all_hosts.findHost(address); },
        all_new_hosts);
    update.warmup_state = evaluatePriorityWarmupState(*update.hosts, panic_threshold_percent);
    any_warming_host = any_warming_host || update.warmup_state.has_warming_host;
  }

  // Retain healthy removed hosts with PENDING_DYNAMIC_REMOVAL in any priority that has warming
  // hosts in the updated configuration and has not yet reached the healthy panic threshold.
  // Removed hosts whose address is still in the configuration are replaced by a new host (e.g.
  // because their health check configuration, hostname or locality changed) and are removed as with
  // ignore_health_on_host_removal.
  if (health_checker_ != nullptr && any_warming_host) {
    for (uint32_t priority = 0; priority < updates.size(); ++priority) {
      PriorityUpdate& update = updates[priority];
      const bool retain_in_priority =
          update.warmup_state.has_warming_host && !update.warmup_state.meets_panic_threshold;
      if (!retain_in_priority) {
        continue;
      }
      HostVector hosts_removed;
      for (const HostSharedPtr& host : update.hosts_removed) {
        if (!all_new_hosts.contains(host->address()->asString()) && canRetainHost(*host)) {
          host->healthFlagSet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL);
          update.hosts->push_back(host);
        } else {
          hosts_removed.push_back(host);
        }
      }
      if (hosts_removed.size() != update.hosts_removed.size()) {
        ENVOY_LOG(debug,
                  "EDS retaining {} removed hosts in priority {} of cluster {} during warmup",
                  update.hosts_removed.size() - hosts_removed.size(), priority, info_->name());
      }
      update.hosts_removed = std::move(hosts_removed);
    }
  }

  uint64_t max_host_weight = 1;
  for (uint32_t priority = 0; priority < updates.size(); ++priority) {
    for (const HostSharedPtr& host : *updates[priority].hosts) {
      max_host_weight = std::max<uint64_t>(max_host_weight, host->weight());
    }
  }
  // updateDynamicHostList() sets the maximum host weight of each priority in turn, without the
  // retained hosts: set it for all the hosts of the cluster instead.
  info_->endpointStats().max_host_weight_.set(max_host_weight);

  bool cluster_rebuilt = false;
  const auto update_priority = [&](const uint32_t priority) {
    PriorityUpdate& update = updates[priority];
    LocalityWeightsMap& locality_weights_map = locality_weights_map_[priority];
    LocalityWeightsMap& new_locality_weights_map = priority_state[priority].second;
    // A priority that had retained hosts is always rebuilt, since its retained hosts may have been
    // removed, retained again or added back. A new priority is always created, even without hosts.
    if (update.host_set != nullptr && !update.hosts_updated && !update.had_retained_hosts &&
        update.host_set->weightedPriorityHealth() == weighted_priority_health &&
        update.host_set->overprovisioningFactor() == overprovisioning_factor &&
        locality_weights_map == new_locality_weights_map) {
      return;
    }
    ASSERT(std::all_of(update.hosts->begin(), update.hosts->end(),
                       [&](const auto& host) { return host->priority() == priority; }));
    locality_weights_map = new_locality_weights_map;
    ENVOY_LOG(
        debug, "EDS hosts or locality weights changed for cluster: {} current hosts {} priority {}",
        info_->name(), update.host_set != nullptr ? update.host_set->hosts().size() : 0, priority);

    priority_state_manager.updateClusterPrioritySet(
        priority, std::move(update.hosts), update.hosts_added, update.hosts_removed, std::nullopt,
        weighted_priority_health, overprovisioning_factor);
    cluster_rebuilt = true;
  };
  for (uint32_t priority = 0; priority < updates.size(); ++priority) {
    update_priority(priority);
  }
  return cluster_rebuilt;
}

void EdsClusterImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                          const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // Config failure may happen if Envoy times out waiting for the EDS resource.
  // If it is a timeout, the eds resources cache is enabled,
  // and there is a cached ClusterLoadAssignment, then the cached assignment should be used.
  if (reason == Envoy::Config::ConfigUpdateFailureReason::FetchTimedout &&
      eds_resources_cache_.has_value()) {
    ENVOY_LOG(trace, "onConfigUpdateFailed due to timeout for {}, looking for cached resources",
              edsServiceName());
    auto cached_resource = eds_resources_cache_->getResource(edsServiceName(), this);
    if (cached_resource.has_value()) {
      ENVOY_LOG(
          debug,
          "Did not receive EDS response on time, using cached ClusterLoadAssignment for cluster {}",
          edsServiceName());
      info_->configUpdateStats().assignment_use_cached_.inc();
      using_cached_resource_ = true;
      update(*cached_resource);
      return;
    }
  }
  // We need to allow server startup to continue, even if we have a bad config.
  onPreInitComplete();
}

absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
EdsClusterFactory::createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                                     ClusterFactoryContext& context) {
  // TODO(kbaichoo): EDS cluster should be able to support loading it's
  // configuration from the CustomClusterType protobuf. Currently it does not.
  // See: https://github.com/envoyproxy/envoy/issues/28752
  if (!cluster.has_eds_cluster_config()) {
    return absl::InvalidArgumentError("cannot create an EDS cluster without an EDS config");
  }

  absl::StatusOr<std::unique_ptr<EdsClusterImpl>> cluster_or_error =
      EdsClusterImpl::create(cluster, context);
  RETURN_IF_NOT_OK(cluster_or_error.status());
  return std::make_pair(std::move(*cluster_or_error), nullptr);
}

bool EdsClusterImpl::validateAllLedsUpdated() const {
  // Iterate through all LEDS based localities, and if they are all updated return true.
  for (const auto& [_, leds_subscription] : leds_localities_) {
    if (!leds_subscription->isUpdated()) {
      return false;
    }
  }
  return true;
}

/**
 * Static registration for the Eds cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(EdsClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy
