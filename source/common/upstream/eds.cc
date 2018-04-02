#include "common/upstream/eds.h"

#include "envoy/api/v2/eds.pb.validate.h"
#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/config/metadata.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/sds_subscription.h"

namespace Envoy {
namespace Upstream {

EdsClusterImpl::EdsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                               Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                               const LocalInfo::LocalInfo& local_info, ClusterManager& cm,
                               Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                               bool added_via_api)
    : BaseDynamicClusterImpl(cluster, cm.bindConfig(), runtime, stats, ssl_context_manager,
                             added_via_api),
      cm_(cm), local_info_(local_info),
      cluster_name_(cluster.eds_cluster_config().service_name().empty()
                        ? cluster.name()
                        : cluster.eds_cluster_config().service_name()) {
  Config::Utility::checkLocalInfo("eds", local_info);

  const auto& eds_config = cluster.eds_cluster_config().eds_config();
  subscription_ = Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::ClusterLoadAssignment>(
      eds_config, local_info.node(), dispatcher, cm, random, info_->statsScope(),
      [this, &eds_config, &cm, &dispatcher,
       &random]() -> Config::Subscription<envoy::api::v2::ClusterLoadAssignment>* {
        return new SdsSubscription(info_->stats(), eds_config, cm, dispatcher, random);
      },
      "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints",
      "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints");
}

void EdsClusterImpl::startPreInit() { subscription_->start({cluster_name_}, *this); }

void EdsClusterImpl::onConfigUpdate(const ResourceVector& resources) {
  typedef std::unique_ptr<HostVector> HostListPtr;
  std::vector<std::pair<HostListPtr, LocalityWeightsMap>> priority_state(1);
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
  for (const auto& locality_lb_endpoint : cluster_load_assignment.endpoints()) {
    const uint32_t priority = locality_lb_endpoint.priority();
    if (priority > 0 && !cluster_name_.empty() && cluster_name_ == cm_.localClusterName()) {
      throw EnvoyException(
          fmt::format("Unexpected non-zero priority for local cluster '{}'.", cluster_name_));
    }
    if (priority_state.size() <= priority) {
      priority_state.resize(priority + 1);
    }
    if (priority_state[priority].first == nullptr) {
      priority_state[priority].first.reset(new HostVector());
    }
    if (locality_lb_endpoint.has_locality() && locality_lb_endpoint.has_load_balancing_weight()) {
      priority_state[priority].second[locality_lb_endpoint.locality()] =
          locality_lb_endpoint.load_balancing_weight().value();
    }
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      priority_state[priority].first->emplace_back(new HostImpl(
          info_, "", resolveProtoAddress(lb_endpoint.endpoint().address()), lb_endpoint.metadata(),
          lb_endpoint.load_balancing_weight().value(), locality_lb_endpoint.locality()));
      const auto& health_status = lb_endpoint.health_status();
      if (health_status == envoy::api::v2::core::HealthStatus::UNHEALTHY ||
          health_status == envoy::api::v2::core::HealthStatus::DRAINING ||
          health_status == envoy::api::v2::core::HealthStatus::TIMEOUT) {
        priority_state[priority].first->back()->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
      }
    }
  }

  for (size_t i = 0; i < priority_state.size(); ++i) {
    if (priority_state[i].first != nullptr) {
      updateHostsPerLocality(priority_set_.getOrCreateHostSet(i), *priority_state[i].first,
                             priority_state[i].second);
    }
  }

  // If we didn't setup to initialize when our first round of health checking is complete, just
  // do it now.
  onPreInitComplete();
}

void EdsClusterImpl::updateHostsPerLocality(HostSet& host_set, const HostVector& new_hosts,
                                            LocalityWeightsMap& locality_weights_map) {
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
  if (updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added, hosts_removed,
                            health_checker_ != nullptr) ||
      current_locality_weights_map_ != locality_weights_map) {
    current_locality_weights_map_ = locality_weights_map;
    LocalityWeightsSharedPtr locality_weights;
    ENVOY_LOG(debug, "EDS hosts or locality weights changed for cluster: {} ({}) priority {}",
              info_->name(), host_set.hosts().size(), host_set.priority());
    std::vector<HostVector> per_locality;

    // If we are configured for locality weighted LB we populate the locality
    // weights.
    const bool locality_weighted_lb = info()->lbConfig().has_locality_weighted_lb_config();
    if (locality_weighted_lb) {
      locality_weights = std::make_shared<LocalityWeights>();
    }
    // If local locality is not defined then skip populating per locality hosts.
    const auto& local_locality = local_info_.node().locality();
    ENVOY_LOG(trace, "Local locality: {}", local_info_.node().locality().DebugString());

    // We use std::map to guarantee a stable ordering for zone aware routing.
    std::map<envoy::api::v2::core::Locality, HostVector, LocalityLess> hosts_per_locality;

    for (const HostSharedPtr& host : *current_hosts_copy) {
      hosts_per_locality[host->locality()].push_back(host);
    }

    // Do we have hosts for the local locality?
    const bool non_empty_local_locality =
        local_info_.node().has_locality() &&
        hosts_per_locality.find(local_locality) != hosts_per_locality.end();

    // As per HostsPerLocality::get(), the per_locality vector must have the
    // local locality hosts first if non_empty_local_locality.
    if (non_empty_local_locality) {
      per_locality.emplace_back(hosts_per_locality[local_locality]);
      if (locality_weighted_lb) {
        locality_weights->emplace_back(locality_weights_map[local_locality]);
      }
    }

    // After the local locality hosts (if any), we place the remaining locality
    // host groups in lexicographic order. This provides a stable ordering for
    // zone aware routing.
    for (auto& entry : hosts_per_locality) {
      if (!non_empty_local_locality || !LocalityEqualTo()(local_locality, entry.first)) {
        per_locality.emplace_back(entry.second);
        if (locality_weighted_lb) {
          locality_weights->emplace_back(locality_weights_map[entry.first]);
        }
      }
    }

    auto per_locality_shared =
        std::make_shared<HostsPerLocalityImpl>(std::move(per_locality), non_empty_local_locality);

    host_set.updateHosts(current_hosts_copy, createHealthyHostList(*current_hosts_copy),
                         per_locality_shared, createHealthyHostLists(*per_locality_shared),
                         std::move(locality_weights), hosts_added, hosts_removed);
  }
}

void EdsClusterImpl::onConfigUpdateFailed(const EnvoyException* e) {
  UNREFERENCED_PARAMETER(e);
  // We need to allow server startup to continue, even if we have a bad config.
  onPreInitComplete();
}

} // namespace Upstream
} // namespace Envoy
