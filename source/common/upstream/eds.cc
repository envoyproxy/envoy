#include "common/upstream/eds.h"

#include "envoy/common/exception.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/upstream/sds_subscription.h"

namespace Envoy {
namespace Upstream {

EdsClusterImpl::EdsClusterImpl(const Json::Object& config, Runtime::Loader& runtime,
                               Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                               const SdsConfig& sds_config, const LocalInfo::LocalInfo& local_info,
                               ClusterManager& cm, Event::Dispatcher& dispatcher,
                               Runtime::RandomGenerator& random)
    : BaseDynamicClusterImpl(config, runtime, stats, ssl_context_manager), local_info_(local_info),
      cluster_name_(config.getString("service_name")) {
  // TODO(htuch): This is where the v2 Subscription implementations will be constructed based on
  // config. Today, we just reuse the v1 SDS config and an adapter Subscription to the v1 REST JSON
  // API.
  subscription_.reset(new SdsSubscription(info_->stats(), sds_config, cm, dispatcher, random));
}

void EdsClusterImpl::initialize() { subscription_->start({cluster_name_}, *this); }

void EdsClusterImpl::onConfigUpdate(const ResourceVector& resources) {
  std::vector<HostSharedPtr> new_hosts;
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected EDS resource length: {}", resources.size()));
  }
  const auto& cluster_load_assignment = resources[0];
  if (cluster_load_assignment.cluster_name() != cluster_name_) {
    throw EnvoyException(
        fmt::format("Unexpected EDS cluster: {}", cluster_load_assignment.cluster_name()));
  }
  for (const auto& locality_lb_endpoint : cluster_load_assignment.endpoints()) {
    const std::string& zone = locality_lb_endpoint.locality().zone();

    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      new_hosts.emplace_back(new HostImpl(
          info_, "", Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(
                         lb_endpoint.endpoint().address().socket_address().ip_address(),
                         lb_endpoint.endpoint().address().socket_address().port().value())},
          lb_endpoint.canary().value(), lb_endpoint.load_balancing_weight().value(), zone));
    }
  }

  HostVectorSharedPtr current_hosts_copy(new std::vector<HostSharedPtr>(hosts()));
  std::vector<HostSharedPtr> hosts_added;
  std::vector<HostSharedPtr> hosts_removed;
  if (updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added, hosts_removed,
                            health_checker_ != nullptr)) {
    ENVOY_LOG(debug, "EDS hosts changed for cluster: {} ({})", info_->name(), hosts().size());
    HostListsSharedPtr per_zone(new std::vector<std::vector<HostSharedPtr>>());

    // If local zone name is not defined then skip populating per zone hosts.
    if (!local_info_.zoneName().empty()) {
      std::map<std::string, std::vector<HostSharedPtr>> hosts_per_zone;

      for (const HostSharedPtr& host : *current_hosts_copy) {
        hosts_per_zone[host->zone()].push_back(host);
      }

      // Populate per_zone hosts only if upstream cluster has hosts in the same zone.
      if (hosts_per_zone.find(local_info_.zoneName()) != hosts_per_zone.end()) {
        per_zone->push_back(hosts_per_zone[local_info_.zoneName()]);

        for (auto& entry : hosts_per_zone) {
          if (local_info_.zoneName() != entry.first) {
            per_zone->push_back(entry.second);
          }
        }
      }
    }

    updateHosts(current_hosts_copy, createHealthyHostList(*current_hosts_copy), per_zone,
                createHealthyHostLists(*per_zone), hosts_added, hosts_removed);

    if (initialize_callback_ && health_checker_ && pending_health_checks_ == 0) {
      pending_health_checks_ = hosts().size();
      ASSERT(pending_health_checks_ > 0);
      health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, bool) -> void {
        if (pending_health_checks_ > 0 && --pending_health_checks_ == 0) {
          initialize_callback_();
          initialize_callback_ = nullptr;
        }
      });
    }
  }

  // If we didn't setup to initialize when our first round of health checking is complete, just
  // do it now.
  if (initialize_callback_ && pending_health_checks_ == 0) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // Upstream
} // Envoy
