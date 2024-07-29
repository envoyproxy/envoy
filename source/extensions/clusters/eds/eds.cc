#include "source/extensions/clusters/eds/eds.h"

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/grpc/common.h"

namespace Envoy {
namespace Upstream {

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
      Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          cluster_context.messageValidationVisitor(), "cluster_name"),
      local_info_(cluster_context.serverFactoryContext().localInfo()),
      eds_resources_cache_(
          Runtime::runtimeFeatureEnabled("envoy.restart_features.use_eds_cache_for_ads")
              ? cluster_context.clusterManager().edsResourcesCache()
              : absl::nullopt) {
  Event::Dispatcher& dispatcher = cluster_context.serverFactoryContext().mainThreadDispatcher();
  assignment_timeout_ = dispatcher.createTimer([this]() -> void { onAssignmentTimeout(); });
  const auto& eds_config = cluster.eds_cluster_config().eds_config();
  if (Config::SubscriptionFactory::isPathBasedConfigSource(
          eds_config.config_source_specifier_case())) {
    initialize_phase_ = InitializePhase::Primary;
  } else {
    initialize_phase_ = InitializePhase::Secondary;
  }
  const auto resource_name = getResourceName();
  subscription_ = THROW_OR_RETURN_VALUE(
      cluster_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          eds_config, Grpc::Common::typeUrl(resource_name), info_->statsScope(), *this,
          resource_decoder_, {}),
      Config::SubscriptionPtr);
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
  PriorityStateManager priority_state_manager(parent_, parent_.local_info_, &host_update_cb,
                                              parent_.random_);
  for (const auto& locality_lb_endpoint : cluster_load_assignment_.endpoints()) {
    THROW_IF_NOT_OK(parent_.validateEndpointsForZoneAwareRouting(locality_lb_endpoint));

    priority_state_manager.initializePriorityFor(locality_lb_endpoint);

    if (locality_lb_endpoint.has_leds_cluster_locality_config()) {
      // The locality uses LEDS, fetch its dynamic data, which must be ready, or otherwise
      // the batchUpdate method should not have been called.
      const auto& leds_config = locality_lb_endpoint.leds_cluster_locality_config();

      // The batchUpdate call must be performed after all the endpoints of all localities
      // were received.
      ASSERT(parent_.leds_localities_.find(leds_config) != parent_.leds_localities_.end() &&
             parent_.leds_localities_[leds_config]->isUpdated());
      for (const auto& [_, lb_endpoint] :
           parent_.leds_localities_[leds_config]->getEndpointsMap()) {
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
  HostMapConstSharedPtr all_hosts = parent_.prioritySet().crossPriorityHostMap();
  ASSERT(all_hosts != nullptr);

  const uint32_t overprovisioning_factor = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      cluster_load_assignment_.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);
  const bool weighted_priority_health =
      cluster_load_assignment_.policy().weighted_priority_health();

  LocalityWeightsMap empty_locality_map;

  // Loop over all priorities that exist in the new configuration.
  auto& priority_state = priority_state_manager.priorityState();
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
      address_list.emplace_back(
          THROW_OR_RETURN_VALUE(parent_.resolveProtoAddress(additional_address.address()),
                                const Network::Address::InstanceConstSharedPtr));
    }
  }

  // When the configuration contains duplicate hosts, only the first one will be retained.
  const auto address_as_string = address->asString();
  if (all_new_hosts.contains(address_as_string)) {
    return;
  }

  priority_state_manager.registerHostForPriority(lb_endpoint.endpoint().hostname(), address,
                                                 address_list, locality_lb_endpoint, lb_endpoint,
                                                 parent_.time_source_);
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
    return absl::InvalidArgumentError(
        fmt::format("Unexpected EDS resource length: {}", resources.size()));
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment =
      dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
          resources[0].get().resource());
  if (cluster_load_assignment.cluster_name() != edsServiceName()) {
    return absl::InvalidArgumentError(fmt::format("Unexpected EDS cluster (expecting {}): {}",
                                                  edsServiceName(),
                                                  cluster_load_assignment.cluster_name()));
  }
  // Validate that each locality doesn't have both LEDS and endpoints defined.
  // TODO(adisuissa): This is only needed for the API v3 support. In future major versions
  // the oneof definition will take care of it.
  for (const auto& locality : cluster_load_assignment.endpoints()) {
    if (locality.has_leds_cluster_locality_config() && locality.lb_endpoints_size() > 0) {
      return absl::InvalidArgumentError(fmt::format(
          "A ClusterLoadAssignment for cluster {} cannot include both LEDS (resource: {}) and a "
          "list of endpoints.",
          edsServiceName(), locality.leds_cluster_locality_config().leds_collection_name()));
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

  // Drop overload configuration parsing.
  absl::Status status = parseDropOverloadConfig(cluster_load_assignment);
  if (!status.ok()) {
    return status;
  }

  // Pause LEDS messages until the EDS config is finished processing.
  Config::ScopedResume maybe_resume_leds;
  if (transport_factory_context_->clusterManager().adsMux()) {
    const auto type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();
    maybe_resume_leds = transport_factory_context_->clusterManager().adsMux()->pause(type_url);
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

  // In case LEDS is used, store the cluster load assignment as a field
  // (optimize for no-copy).
  const envoy::config::endpoint::v3::ClusterLoadAssignment* used_load_assignment;
  if (!cla_leds_configs.empty() || eds_resources_cache_.has_value()) {
    cluster_load_assignment_ = std::make_unique<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        std::move(cluster_load_assignment));
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
          [&, used_load_assignment]() {
            // Called upon an update to the locality.
            if (validateAllLedsUpdated()) {
              BatchUpdateHelper helper(*this, *used_load_assignment);
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
  update(resource);

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
  update(resource);
}

void EdsClusterImpl::reloadHealthyHostsHelper(const HostSharedPtr& host) {
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

    prioritySet().updateHosts(priority,
                              HostSetImpl::partitionHosts(hosts_copy, hosts_per_locality_copy),
                              host_set->localityWeights(), {}, hosts_to_remove, random_.random(),
                              absl::nullopt, absl::nullopt);
  }
}

bool EdsClusterImpl::updateHostsPerLocality(
    const uint32_t priority, bool weighted_priority_health, const uint32_t overprovisioning_factor,
    const HostVector& new_hosts, LocalityWeightsMap& locality_weights_map,
    LocalityWeightsMap& new_locality_weights_map, PriorityStateManager& priority_state_manager,
    const HostMap& all_hosts, const absl::flat_hash_set<std::string>& all_new_hosts) {
  const auto& host_set = priority_set_.getOrCreateHostSet(priority, overprovisioning_factor);
  HostVectorSharedPtr current_hosts_copy(new HostVector(host_set.hosts()));

  HostVector hosts_added;
  HostVector hosts_removed;
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
  const bool hosts_updated = updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added,
                                                   hosts_removed, all_hosts, all_new_hosts);
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
        priority, std::move(current_hosts_copy), hosts_added, hosts_removed, absl::nullopt,
        weighted_priority_health, overprovisioning_factor);
    return true;
  }
  return false;
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
      envoy::config::endpoint::v3::ClusterLoadAssignment cached_load_assignment =
          dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(*cached_resource);
      info_->configUpdateStats().assignment_use_cached_.inc();
      using_cached_resource_ = true;
      update(cached_load_assignment);
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
