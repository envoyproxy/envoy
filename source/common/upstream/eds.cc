#include "common/upstream/eds.h"

#include "envoy/api/v2/eds.pb.validate.h"

#include "common/config/subscription_factory.h"

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
                        : cluster.eds_cluster_config().service_name()) {
  Config::Utility::checkLocalInfo("eds", local_info_);

  const auto& eds_config = cluster.eds_cluster_config().eds_config();
  Event::Dispatcher& dispatcher = factory_context.dispatcher();
  Runtime::RandomGenerator& random = factory_context.random();
  Upstream::ClusterManager& cm = factory_context.clusterManager();
  subscription_ = Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::ClusterLoadAssignment>(
      eds_config, local_info_, dispatcher, cm, random, info_->statsScope(),
      "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints",
      "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints", factory_context.api());
}

void EdsClusterImpl::startPreInit() { subscription_->start({cluster_name_}, *this); }

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

void EdsClusterImpl::onConfigUpdate(const ResourceVector& resources, const std::string&) {
  if (resources.empty()) {
    ENVOY_LOG(debug, "Missing ClusterLoadAssignment for {} in onConfigUpdate()", cluster_name_);
    info_->stats().update_empty_.inc();
    onPreInitComplete();
    return;
  }
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected EDS resource length: {}", resources.size()));
  }
  const auto& cluster_load_assignment = resources[0];
  MessageUtil::validate(cluster_load_assignment);
  // TODO(PiotrSikora): Remove this hack once fixed internally.
  if (!(cluster_load_assignment.cluster_name() == cluster_name_)) {
    throw EnvoyException(fmt::format("Unexpected EDS cluster (expecting {}): {}", cluster_name_,
                                     cluster_load_assignment.cluster_name()));
  }

  BatchUpdateHelper helper(*this, cluster_load_assignment);
  priority_set_.batchHostUpdate(helper);
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
  // when the locality weight map changes.
  // TODO(htuch): We eagerly update all the host sets here on weight changes, which isn't great,
  // since this has the knock on effect that we rebuild the load balancers and locality scheduler.
  // We could make this happen lazily, as we do for host-level weight updates, where as things age
  // out of the locality scheduler, we discover their new weights. We don't currently have a shared
  // object for locality weights that we can update here, we should add something like this to
  // improve performance and scalability of locality weight updates.
  if (host_set.overprovisioningFactor() != overprovisioning_factor ||
      updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added, hosts_removed,
                            updated_hosts, all_hosts_) ||
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

void EdsClusterImpl::onConfigUpdateFailed(const EnvoyException* e) {
  UNREFERENCED_PARAMETER(e);
  // We need to allow server startup to continue, even if we have a bad config.
  onPreInitComplete();
}

ClusterImplBaseSharedPtr EdsClusterFactory::createClusterImpl(
    const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  if (!cluster.has_eds_cluster_config()) {
    throw EnvoyException("cannot create an EDS cluster without an EDS config");
  }

  return std::make_unique<EdsClusterImpl>(cluster, context.runtime(), socket_factory_context,
                                          std::move(stats_scope), context.addedViaApi());
}

/**
 * Static registration for the strict dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(EdsClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy
