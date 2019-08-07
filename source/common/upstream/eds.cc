#include "common/upstream/eds.h"

#include "envoy/api/v2/eds.pb.validate.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Upstream {

EdsClusterImpl::EdsClusterImpl(
    const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                             added_via_api),
      cm_(factory_context.clusterManager()), local_info_(factory_context.localInfo()),
      cluster_name_(cluster.eds_cluster_config().service_name().empty()
                        ? cluster.name()
                        : cluster.eds_cluster_config().service_name()),
      validation_visitor_(factory_context.messageValidationVisitor()) {
  Event::Dispatcher& dispatcher = factory_context.dispatcher();
  assignment_timeout_ = dispatcher.createTimer([this]() -> void { onAssignmentTimeout(); });
  const auto& eds_config = cluster.eds_cluster_config().eds_config();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          eds_config,
          Grpc::Common::typeUrl(
              envoy::api::v2::ClusterLoadAssignment().GetDescriptor()->full_name()),
          info_->statsScope(), *this);
}

void EdsClusterImpl::startPreInit() { subscription_->start({cluster_name_}); }

void EdsClusterImpl::BatchUpdateHelper::batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) {
  std::unordered_map<std::string, HostSharedPtr> updated_hosts;
  PriorityStateManager priority_state_manager(parent_, parent_.local_info_, &host_update_cb);
  for (const auto& locality_lb_endpoint : cluster_load_assignment_.endpoints()) {
    const uint32_t priority = locality_lb_endpoint.priority();

    if (priority > 0 && !parent_.cluster_name_.empty() &&
        parent_.cluster_name_ == parent_.cm_.localClusterName()) {
      throw EnvoyException(fmt::format("Unexpected non-zero priority for local cluster '{}'.",
                                       parent_.cluster_name_));
    }
    priority_state_manager.initializePriorityFor(locality_lb_endpoint);

    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      priority_state_manager.registerHostForPriority(
          "", parent_.resolveProtoAddress(lb_endpoint.endpoint().address()), locality_lb_endpoint,
          lb_endpoint);
    }
  }

  // Track whether we rebuilt any LB structures.
  bool cluster_rebuilt = false;

  const uint32_t overprovisioning_factor = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      cluster_load_assignment_.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);

  // Loop over all priorities that exist in the new configuration.
  auto& priority_state = priority_state_manager.priorityState();
  for (size_t i = 0; i < priority_state.size(); ++i) {
    if (priority_state[i].first != nullptr) {
      if (parent_.locality_weights_map_.size() <= i) {
        parent_.locality_weights_map_.resize(i + 1);
      }
      cluster_rebuilt |= parent_.updateHostsPerLocality(
          i, overprovisioning_factor, *priority_state[i].first, parent_.locality_weights_map_[i],
          priority_state[i].second, priority_state_manager, updated_hosts);
    }
  }

  // Loop over all priorities not present in the config that already exists. This will
  // empty out any remaining priority that the config update did not refer to.
  for (size_t i = priority_state.size(); i < parent_.priority_set_.hostSetsPerPriority().size();
       ++i) {
    const HostVector empty_hosts;
    LocalityWeightsMap empty_locality_map;

    if (parent_.locality_weights_map_.size() <= i) {
      parent_.locality_weights_map_.resize(i + 1);
    }
    cluster_rebuilt |= parent_.updateHostsPerLocality(
        i, overprovisioning_factor, empty_hosts, parent_.locality_weights_map_[i],
        empty_locality_map, priority_state_manager, updated_hosts);
  }

  parent_.all_hosts_ = std::move(updated_hosts);

  if (!cluster_rebuilt) {
    parent_.info_->stats().update_no_rebuild_.inc();
  }

  // If we didn't setup to initialize when our first round of health checking is complete, just
  // do it now.
  parent_.onPreInitComplete();
}

void EdsClusterImpl::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                    const std::string&) {
  if (!validateUpdateSize(resources.size())) {
    return;
  }
  auto cluster_load_assignment = MessageUtil::anyConvert<envoy::api::v2::ClusterLoadAssignment>(
      resources[0], validation_visitor_);
  MessageUtil::validate(cluster_load_assignment);
  if (cluster_load_assignment.cluster_name() != cluster_name_) {
    throw EnvoyException(fmt::format("Unexpected EDS cluster (expecting {}): {}", cluster_name_,
                                     cluster_load_assignment.cluster_name()));
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
    info_->stats().assignment_timeout_received_.inc();
    assignment_timeout_->enableTimer(std::chrono::milliseconds(stale_after_ms));
  }

  BatchUpdateHelper helper(*this, cluster_load_assignment);
  priority_set_.batchHostUpdate(helper);
}

void EdsClusterImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& resources,
    const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
  if (!validateUpdateSize(resources.size())) {
    return;
  }
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> unwrapped_resource;
  *unwrapped_resource.Add() = resources[0].resource();
  onConfigUpdate(unwrapped_resource, resources[0].version());
}

bool EdsClusterImpl::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    ENVOY_LOG(debug, "Missing ClusterLoadAssignment for {} in onConfigUpdate()", cluster_name_);
    info_->stats().update_empty_.inc();
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
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  envoy::api::v2::ClusterLoadAssignment resource;
  resource.set_cluster_name(cluster_name_);
  resources.Add()->PackFrom(resource);
  onConfigUpdate(resources, "");
  // Stat to track how often we end up with stale assignments.
  info_->stats().assignment_stale_.inc();
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

  if (host_to_exclude != nullptr) {
    ASSERT(all_hosts_.find(host_to_exclude->address()->asString()) != all_hosts_.end());
    all_hosts_.erase(host_to_exclude->address()->asString());
  }
}

bool EdsClusterImpl::updateHostsPerLocality(
    const uint32_t priority, const uint32_t overprovisioning_factor, const HostVector& new_hosts,
    LocalityWeightsMap& locality_weights_map, LocalityWeightsMap& new_locality_weights_map,
    PriorityStateManager& priority_state_manager,
    std::unordered_map<std::string, HostSharedPtr>& updated_hosts) {
  const auto& host_set = priority_set_.getOrCreateHostSet(priority, overprovisioning_factor);
  HostVectorSharedPtr current_hosts_copy(new HostVector(host_set.hosts()));

  HostVector hosts_added;
  HostVector hosts_removed;
  // We need to trigger updateHosts with the new host vectors if they have changed. We also do this
  // when the locality weight map or the overprovisioning factor. Note calling updateDynamicHostList
  // is responsible for both determining whether there was a change and to perform the actual update
  // to current_hosts_copy, so it must be called even if we know that we need to update (e.g. if the
  // overprovisioning factor changes).
  // TODO(htuch): We eagerly update all the host sets here on weight changes, which isn't great,
  // since this has the knock on effect that we rebuild the load balancers and locality scheduler.
  // We could make this happen lazily, as we do for host-level weight updates, where as things age
  // out of the locality scheduler, we discover their new weights. We don't currently have a shared
  // object for locality weights that we can update here, we should add something like this to
  // improve performance and scalability of locality weight updates.
  const bool hosts_updated = updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added,
                                                   hosts_removed, updated_hosts, all_hosts_);
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
  //  We should not call onPreInitComplete if this method is called because of stream disconnection.
  // This might potentially hang the initialization forever, if init_fetch_timeout is disabled.
  if (reason == Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure) {
    return;
  }
  // We need to allow server startup to continue, even if we have a bad config.
  onPreInitComplete();
}

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
EdsClusterFactory::createClusterImpl(
    const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  if (!cluster.has_eds_cluster_config()) {
    throw EnvoyException("cannot create an EDS cluster without an EDS config");
  }

  return std::make_pair(
      std::make_shared<EdsClusterImpl>(cluster, context.runtime(), socket_factory_context,
                                       std::move(stats_scope), context.addedViaApi()),
      nullptr);
}

/**
 * Static registration for the strict dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(EdsClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy
