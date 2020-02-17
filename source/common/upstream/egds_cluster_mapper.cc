#include "common/upstream/egds_cluster_mapper.h"

#include "common/network/resolver_impl.h"

namespace Envoy {
namespace Upstream {

EgdsClusterMapper::EgdsClusterMapper(
    EndpointGroupMonitorManager& monitor_manager, Delegate& delegate,
    BaseDynamicClusterImpl& cluster,
    const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment,
    const LocalInfo::LocalInfo& local_info)
    : monitor_manager_(monitor_manager), delegate_(delegate), cluster_(cluster),
      origin_cluster_load_assignment_(cluster_load_assignment), local_info_(local_info) {
  for (const auto& config : cluster_load_assignment.endpoint_groups()) {
    addResource(config.endpoint_group_name());
  }

  origin_cluster_load_assignment_.clear_endpoints();
  origin_cluster_load_assignment_.clear_named_endpoints();
  origin_cluster_load_assignment_.clear_endpoint_groups();
}

std::set<std::string> EgdsClusterMapper::egds_resource_names() const {
  std::set<std::string> names;
  std::transform(active_monitors_.begin(), active_monitors_.end(),
                 std::inserter(names, names.end()), [](auto pair) { return pair.first; });
  return names;
}

bool EgdsClusterMapper::resourceExists(absl::string_view name) const {
  return active_monitors_.find(name) != active_monitors_.end();
}

void EgdsClusterMapper::addResource(absl::string_view name) {
  if (active_monitors_.find(name) == active_monitors_.end()) {
    active_monitors_.emplace(name, std::make_shared<ActiveEndpointGroupMonitor>(*this));
    monitor_manager_.addMonitor(active_monitors_[name], name);
  }
}

void EgdsClusterMapper::removeResource(absl::string_view name) {
  if (active_monitors_.find(name) != active_monitors_.end()) {
    monitor_manager_.removeMonitor(active_monitors_[name], name);
    active_monitors_.erase(name);
  }
}

void EgdsClusterMapper::ActiveEndpointGroupMonitor::update(
    const envoy::config::endpoint::v3::EndpointGroup& group, absl::string_view version_info) {
  batchUpdate(group, version_info, true);
}

void EgdsClusterMapper::ActiveEndpointGroupMonitor::batchUpdate(
    const envoy::config::endpoint::v3::EndpointGroup& group, absl::string_view version_info,
    bool all_endpoint_groups_updated) {
  if (group_.has_value() && Protobuf::util::MessageDifferencer::Equivalent(group_.value(), group)) {
    ENVOY_LOG(info, "Resources {} are unchanged and do not need to be updated, version: {} ",
              group.name(), version_info);
    return;
  }

  group_ = group;
  version_ = version_info.data();

  // When the complete data of the cluster is initialized, subsequent updates to each EG are
  // incrementally calculated.
  if (parent_.cluster_initialized_) {
    parent_.addUpdatedActiveMonitor(shared_from_this());
    if (all_endpoint_groups_updated) {
      // This is the last updated endpoint group, trigger a batch update.
      parent_.batchHostUpdate();
      return;
    }

    return;
  }

  // Initialize the Endpoint Group, using the initial data as the baseline for subsequent
  // calculations. Initialization does not trigger "updateHosts" notification.
  initializeEndpointGroup();

  if (parent_.clusterDataIsReady()) {
    // Initialize the cluster after all EG data is complete and the initialization is performed only
    // once.
    parent_.initializeCluster();
  }
}

void EgdsClusterMapper::ActiveEndpointGroupMonitor::initializeEndpointGroup() {
  EmptyBatchUpdateScope empty_update;
  doBatchUpdate(empty_update);
}

void EgdsClusterMapper::ActiveEndpointGroupMonitor::doBatchUpdate(
    PrioritySet::HostUpdateCb& host_update_cb) {
  std::unordered_map<std::string, HostSharedPtr> updated_hosts;
  PriorityStateManager priority_state_manager(parent_.cluster_, parent_.local_info_,
                                              &host_update_cb);
  for (const auto& locality_lb_endpoint : group_.value().endpoints()) {
    priority_state_manager.initializePriorityFor(locality_lb_endpoint);

    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      priority_state_manager.registerHostForPriority(
          "", Network::Address::resolveProtoAddress(lb_endpoint.endpoint().address()),
          locality_lb_endpoint, lb_endpoint);
    }
  }

  // Track whether we rebuilt any LB structures.
  bool endpoint_group_rebuilt = false;

  const uint32_t overprovisioning_factor =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(parent_.origin_cluster_load_assignment_.policy(),
                                      overprovisioning_factor, kDefaultOverProvisioningFactor);

  // Loop over all priorities that exist in the new configuration.
  auto& priority_state = priority_state_manager.priorityState();
  for (size_t i = 0; i < priority_state.size(); ++i) {
    if (priority_state[i].first != nullptr) {
      endpoint_group_rebuilt |= calculateUpdatedHostsPerLocality(
          i, *priority_state[i].first, updated_hosts, priority_state_manager,
          priority_state[i].second, overprovisioning_factor);
    }
  }

  all_hosts_ = std::move(updated_hosts);

  if (endpoint_group_rebuilt) {
    // TODO(leilei.gll) Add stats.
  }

  ENVOY_LOG(debug, "Edgs resource '{}' completes batch updates", group_.value().name());
}

bool EgdsClusterMapper::ActiveEndpointGroupMonitor::calculateUpdatedHostsPerLocality(
    const uint32_t priority, const HostVector& new_hosts,
    std::unordered_map<std::string, HostSharedPtr>& updated_hosts,
    PriorityStateManager& priority_state_manager, LocalityWeightsMap& new_locality_weights_map,
    absl::optional<uint32_t> overprovisioning_factor) {
  auto& host_set = priority_set_.getOrCreateMutableHostSet(priority);
  ENVOY_LOG(debug,
            "compute the updated host in the '{}' endpoint-group, added hosts count '{}', existed "
            "hosts count '{}'",
            group_.value().name(), new_hosts.size(), host_set.mutableHosts().size());
  HostVector hosts_added;
  HostVector hosts_removed;
  const bool hosts_updated = parent_.cluster_.updateDynamicHostList(
      new_hosts, host_set.mutableHosts(), hosts_added, hosts_removed, updated_hosts, all_hosts_);
  if (hosts_updated && parent_.cluster_initialized_) {
    parent_.delegate_.updateHosts(priority, hosts_added, hosts_removed, priority_state_manager,
                                  new_locality_weights_map, overprovisioning_factor);
  }

  return hosts_updated;
}

bool EgdsClusterMapper::clusterDataIsReady() {
  for (const auto& pair : active_monitors_) {
    if (!pair.second->group_.has_value()) {
      return false;
    }
  }

  return (cluster_initialized_ = true);
}

void EgdsClusterMapper::initializeCluster() {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment(
      origin_cluster_load_assignment_);
  for (const auto& pair : active_monitors_) {
    cluster_load_assignment.mutable_endpoints()->MergeFrom(pair.second->group_.value().endpoints());
    cluster_load_assignment.mutable_named_endpoints()->insert(
        pair.second->group_.value().named_endpoints().begin(),
        pair.second->group_.value().named_endpoints().end());
  }

  ENVOY_LOG(debug, "Egds cluster {} updated, endpoint count {}",
            cluster_load_assignment.cluster_name(), cluster_load_assignment.endpoints_size());
  delegate_.initializeCluster(cluster_load_assignment);
}

void EgdsClusterMapper::BatchUpdateHelper::batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) {
  for (auto& monitor : updated_monitor_) {
    monitor->doBatchUpdate(host_update_cb);
  }
}

void EgdsClusterMapper::batchHostUpdate() {
  cluster_.prioritySet().batchHostUpdate(batch_update_helper_);
  batch_update_helper_.clear();
}

void EgdsClusterMapper::addUpdatedActiveMonitor(ActiveEndpointGroupMonitorSharedPtr monitor) {
  batch_update_helper_.addUpdatedMonitor(monitor);
}

} // namespace Upstream
} // namespace Envoy
