#include "source/extensions/clusters/eds/eds.h"

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/config/decoded_resource_impl.h"

namespace Envoy {
namespace Upstream {

EdsClusterImpl::EdsClusterImpl(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopeSharedPtr&& stats_scope, bool added_via_api)
    : BaseDynamicClusterImpl(server_context, cluster, runtime, factory_context,
                             std::move(stats_scope), added_via_api,
                             factory_context.mainThreadDispatcher().timeSource()),
      Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          factory_context.messageValidationVisitor(), "cluster_name"),
      factory_context_(factory_context), local_info_(factory_context.localInfo()),
      cluster_name_(cluster.eds_cluster_config().service_name().empty()
                        ? cluster.name()
                        : cluster.eds_cluster_config().service_name()) {
  Event::Dispatcher& dispatcher = factory_context.mainThreadDispatcher();
  assignment_timeout_ = dispatcher.createTimer([this]() -> void { onAssignmentTimeout(); });
  const auto& eds_config = cluster.eds_cluster_config().eds_config();
  if (Config::SubscriptionFactory::isPathBasedConfigSource(
          eds_config.config_source_specifier_case())) {
    initialize_phase_ = InitializePhase::Primary;
  } else {
    initialize_phase_ = InitializePhase::Secondary;
  }
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          eds_config, Grpc::Common::typeUrl(resource_name), info_->statsScope(), *this,
          resource_decoder_, {});
}

void EdsClusterImpl::startPreInit() { subscription_->start({cluster_name_}); }

void EdsClusterImpl::BatchUpdateHelper::batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) {
  absl::flat_hash_set<std::string> all_new_hosts;
  PriorityStateManager priority_state_manager(parent_, parent_.local_info_, &host_update_cb);
  for (const auto& locality_lb_endpoint : cluster_load_assignment_.endpoints()) {
    parent_.validateEndpointsForZoneAwareRouting(locality_lb_endpoint);

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

  LocalityWeightsMap empty_locality_map;

  // Loop over all priorities that exist in the new configuration.
  auto& priority_state = priority_state_manager.priorityState();
  for (size_t i = 0; i < priority_state.size(); ++i) {
    if (parent_.locality_weights_map_.size() <= i) {
      parent_.locality_weights_map_.resize(i + 1);
    }
    if (priority_state[i].first != nullptr) {
      cluster_rebuilt |= parent_.updateHostsPerLocality(
          i, overprovisioning_factor, *priority_state[i].first, parent_.locality_weights_map_[i],
          priority_state[i].second, priority_state_manager, *all_hosts, all_new_hosts);
    } else {
      // If the new update contains a priority with no hosts, call the update function with an empty
      // set of hosts.
      cluster_rebuilt |= parent_.updateHostsPerLocality(
          i, overprovisioning_factor, {}, parent_.locality_weights_map_[i], empty_locality_map,
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
        i, overprovisioning_factor, {}, parent_.locality_weights_map_[i], empty_locality_map,
        priority_state_manager, *all_hosts, all_new_hosts);
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
  const auto address = parent_.resolveProtoAddress(lb_endpoint.endpoint().address());
  // When the configuration contains duplicate hosts, only the first one will be retained.
  const auto address_as_string = address->asString();
  if (all_new_hosts.count(address_as_string) > 0) {
    return;
  }

  priority_state_manager.registerHostForPriority(lb_endpoint.endpoint().hostname(), address,
                                                 locality_lb_endpoint, lb_endpoint,
                                                 parent_.time_source_);
  all_new_hosts.emplace(address_as_string);
}

void EdsClusterImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                    const std::string&) {
  if (!validateUpdateSize(resources.size())) {
    return;
  }
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment =
      dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
          resources[0].get().resource());
  if (cluster_load_assignment.cluster_name() != cluster_name_) {
    throw EnvoyException(fmt::format("Unexpected EDS cluster (expecting {}): {}", cluster_name_,
                                     cluster_load_assignment.cluster_name()));
  }
  // Validate that each locality doesn't have both LEDS and endpoints defined.
  // TODO(adisuissa): This is only needed for the API v3 support. In future major versions
  // the oneof definition will take care of it.
  for (const auto& locality : cluster_load_assignment.endpoints()) {
    if (locality.has_leds_cluster_locality_config() && locality.lb_endpoints_size() > 0) {
      throw EnvoyException(fmt::format(
          "A ClusterLoadAssignment for cluster {} cannot include both LEDS (resource: {}) and a "
          "list of endpoints.",
          cluster_name_, locality.leds_cluster_locality_config().leds_collection_name()));
    }
  }

  // Disable timer (if enabled) as we have received new assignment.
  if (assignment_timeout_->enabled()) {
    assignment_timeout_->disableTimer();
  }
  // Check if endpoint_stale_after is set.
  const uint64_t stale_after_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(cluster_load_assignment.policy(), endpoint_stale_after, 0);
  if (stale_after_ms > 0) {
    // Stat to track how often we receive valid assignment_timeout in response.
    info_->configUpdateStats().assignment_timeout_received_.inc();
    assignment_timeout_->enableTimer(std::chrono::milliseconds(stale_after_ms));
  }

  // Pause LEDS messages until the EDS config is finished processing.
  Config::ScopedResume maybe_resume_leds;
  if (factory_context_.clusterManager().adsMux()) {
    const auto type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();
    maybe_resume_leds = factory_context_.clusterManager().adsMux()->pause(type_url);
  }

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
  envoy::config::endpoint::v3::ClusterLoadAssignment* used_load_assignment;
  if (cla_leds_configs.empty()) {
    cluster_load_assignment_ = absl::nullopt;
    used_load_assignment = &cluster_load_assignment;
  } else {
    cluster_load_assignment_ = std::move(cluster_load_assignment);
    used_load_assignment = &cluster_load_assignment_.value();
  }

  // Add all the LEDS localities that are new.
  for (const auto& leds_config : cla_leds_configs) {
    if (leds_localities_.find(leds_config) == leds_localities_.end()) {
      ENVOY_LOG(trace, "Found new LEDS config in EDS onConfigUpdate() for cluster {}: {}",
                cluster_name_, leds_config.DebugString());

      // Create a new LEDS subscription and add it to the subscriptions map.
      LedsSubscriptionPtr leds_locality_subscription = std::make_unique<LedsSubscription>(
          leds_config, cluster_name_, factory_context_, info_->statsScope(),
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

void EdsClusterImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                    const Protobuf::RepeatedPtrField<std::string>&,
                                    const std::string&) {
  if (!validateUpdateSize(added_resources.size())) {
    return;
  }
  onConfigUpdate(added_resources, added_resources[0].get().version());
}

bool EdsClusterImpl::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    ENVOY_LOG(debug, "Missing ClusterLoadAssignment for {} in onConfigUpdate()", cluster_name_);
    info_->configUpdateStats().update_empty_.inc();
    onPreInitComplete();
    return false;
  }
  if (num_resources != 1) {
    throw EnvoyException(fmt::format("Unexpected EDS resource length: {}", num_resources));
    // (would be a return false here)
  }
  return true;
}

void EdsClusterImpl::onAssignmentTimeout() {
  // We can no longer use the assignments, remove them.
  // TODO(vishalpowar) This is not going to work for incremental updates, and we
  // need to instead change the health status to indicate the assignments are
  // stale.
  // TODO(snowp): This should probably just use xDS TTLs?
  envoy::config::endpoint::v3::ClusterLoadAssignment resource;
  resource.set_cluster_name(cluster_name_);
  ProtobufWkt::Any any_resource;
  any_resource.PackFrom(resource);
  auto decoded_resource =
      Config::DecodedResourceImpl::fromResource(*resource_decoder_, any_resource, "");
  std::vector<Config::DecodedResourceRef> resource_refs = {*decoded_resource};
  onConfigUpdate(resource_refs, "");
  // Stat to track how often we end up with stale assignments.
  info_->configUpdateStats().assignment_stale_.inc();
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
                              host_set->localityWeights(), {}, hosts_to_remove, absl::nullopt);
  }
}

bool EdsClusterImpl::updateHostsPerLocality(
    const uint32_t priority, const uint32_t overprovisioning_factor, const HostVector& new_hosts,
    LocalityWeightsMap& locality_weights_map, LocalityWeightsMap& new_locality_weights_map,
    PriorityStateManager& priority_state_manager, const HostMap& all_hosts,
    const absl::flat_hash_set<std::string>& all_new_hosts) {
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
  if (hosts_updated || host_set.overprovisioningFactor() != overprovisioning_factor ||
      locality_weights_map != new_locality_weights_map) {
    ASSERT(std::all_of(current_hosts_copy->begin(), current_hosts_copy->end(),
                       [&](const auto& host) { return host->priority() == priority; }));
    locality_weights_map = new_locality_weights_map;
    ENVOY_LOG(debug,
              "EDS hosts or locality weights changed for cluster: {} current hosts {} priority {}",
              info_->name(), host_set.hosts().size(), host_set.priority());

    priority_state_manager.updateClusterPrioritySet(priority, std::move(current_hosts_copy),
                                                    hosts_added, hosts_removed, absl::nullopt,
                                                    overprovisioning_factor);
    return true;
  }
  return false;
}

void EdsClusterImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                          const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad config.
  onPreInitComplete();
}

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
EdsClusterFactory::createClusterImpl(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopeSharedPtr&& stats_scope) {
  if (!cluster.has_eds_cluster_config()) {
    throw EnvoyException("cannot create an EDS cluster without an EDS config");
  }

  return std::make_pair(std::make_unique<EdsClusterImpl>(
                            server_context, cluster, context.runtime(), socket_factory_context,
                            std::move(stats_scope), context.addedViaApi()),
                        nullptr);
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
