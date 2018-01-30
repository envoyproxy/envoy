#include "common/upstream/eds.h"

#include "envoy/api/v2/eds.pb.validate.h"
#include "envoy/common/exception.h"

#include "common/config/metadata.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/sds_subscription.h"

#include "fmt/format.h"

namespace Envoy {
namespace Upstream {

EdsClusterImpl::EdsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                               Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                               const LocalInfo::LocalInfo& local_info, ClusterManager& cm,
                               Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                               bool added_via_api)
    : BaseDynamicClusterImpl(cluster, cm.sourceAddress(), runtime, stats, ssl_context_manager,
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
  typedef std::unique_ptr<std::vector<HostSharedPtr>> HostListPtr;
  std::vector<HostListPtr> new_hosts(1);
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
    if (new_hosts.size() <= priority) {
      new_hosts.resize(priority + 1);
    }
    if (new_hosts[priority] == nullptr) {
      new_hosts[priority] = HostListPtr{new std::vector<HostSharedPtr>};
    }
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      new_hosts[priority]->emplace_back(new HostImpl(
          info_, "", Network::Address::resolveProtoAddress(lb_endpoint.endpoint().address()),
          lb_endpoint.metadata(), lb_endpoint.load_balancing_weight().value(),
          locality_lb_endpoint.locality()));
    }
  }

  for (size_t i = 0; i < new_hosts.size(); ++i) {
    if (new_hosts[i] != nullptr) {
      updateHostsPerLocality(priority_set_.getOrCreateHostSet(i), *new_hosts[i]);
    }
  }

  // If we didn't setup to initialize when our first round of health checking is complete, just
  // do it now.
  onPreInitComplete();
}

void EdsClusterImpl::updateHostsPerLocality(HostSet& host_set,
                                            std::vector<HostSharedPtr>& new_hosts) {
  HostVectorSharedPtr current_hosts_copy(new std::vector<HostSharedPtr>(host_set.hosts()));

  std::vector<HostSharedPtr> hosts_added;
  std::vector<HostSharedPtr> hosts_removed;
  if (updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added, hosts_removed,
                            health_checker_ != nullptr)) {
    ENVOY_LOG(debug, "EDS hosts changed for cluster: {} ({}) priority {}", info_->name(),
              host_set.hosts().size(), host_set.priority());
    HostListsSharedPtr per_locality(new std::vector<std::vector<HostSharedPtr>>());

    // If local locality is not defined then skip populating per locality hosts.
    const Locality local_locality(local_info_.node().locality());
    ENVOY_LOG(trace, "Local locality: {}", local_info_.node().locality().DebugString());
    if (!local_locality.empty()) {
      std::map<Locality, std::vector<HostSharedPtr>> hosts_per_locality;

      for (const HostSharedPtr& host : *current_hosts_copy) {
        hosts_per_locality[Locality(host->locality())].push_back(host);
      }

      // Populate per_locality hosts only if upstream cluster has hosts in the same locality.
      if (hosts_per_locality.find(local_locality) != hosts_per_locality.end()) {
        per_locality->push_back(hosts_per_locality[local_locality]);

        for (auto& entry : hosts_per_locality) {
          if (local_locality != entry.first) {
            per_locality->push_back(entry.second);
          }
        }
      }
    }

    host_set.updateHosts(current_hosts_copy, createHealthyHostList(*current_hosts_copy),
                         per_locality, createHealthyHostLists(*per_locality), hosts_added,
                         hosts_removed);
  }
}

void EdsClusterImpl::onConfigUpdateFailed(const EnvoyException* e) {
  UNREFERENCED_PARAMETER(e);
  // We need to allow server startup to continue, even if we have a bad config.
  onPreInitComplete();
}

} // namespace Upstream
} // namespace Envoy
