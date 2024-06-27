#include "source/extensions/clusters/strict_dns/strict_dns_cluster.h"

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

namespace Envoy {
namespace Upstream {

StrictDnsClusterImpl::StrictDnsClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                                           ClusterFactoryContext& context,
                                           Network::DnsResolverSharedPtr dns_resolver)
    : BaseDynamicClusterImpl(cluster, context), load_assignment_(cluster.load_assignment()),
      local_info_(context.serverFactoryContext().localInfo()), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      respect_dns_ttl_(cluster.respect_dns_ttl()) {
  failure_backoff_strategy_ =
      Config::Utility::prepareDnsRefreshStrategy<envoy::config::cluster::v3::Cluster>(
          cluster, dns_refresh_rate_ms_.count(),
          context.serverFactoryContext().api().randomGenerator());

  std::list<ResolveTargetPtr> resolve_targets;
  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    THROW_IF_NOT_OK(validateEndpointsForZoneAwareRouting(locality_lb_endpoint));

    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& socket_address = lb_endpoint.endpoint().address().socket_address();
      if (!socket_address.resolver_name().empty()) {
        throw EnvoyException("STRICT_DNS clusters must NOT have a custom resolver name set");
      }

      resolve_targets.emplace_back(new ResolveTarget(
          *this, context.serverFactoryContext().mainThreadDispatcher(), socket_address.address(),
          socket_address.port_value(), locality_lb_endpoint, lb_endpoint));
    }
  }
  resolve_targets_ = std::move(resolve_targets);
  dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);

  overprovisioning_factor_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      load_assignment_.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);
  weighted_priority_health_ = load_assignment_.policy().weighted_priority_health();
}

void StrictDnsClusterImpl::startPreInit() {
  for (const ResolveTargetPtr& target : resolve_targets_) {
    target->startResolve();
  }
  // If the config provides no endpoints, the cluster is initialized immediately as if all hosts are
  // resolved in failure.
  if (resolve_targets_.empty() || !wait_for_warm_on_init_) {
    onPreInitComplete();
  }
}

void StrictDnsClusterImpl::updateAllHosts(const HostVector& hosts_added,
                                          const HostVector& hosts_removed,
                                          uint32_t current_priority) {
  PriorityStateManager priority_state_manager(*this, local_info_, nullptr, random_);
  // At this point we know that we are different so make a new host list and notify.
  //
  // TODO(dio): The uniqueness of a host address resolved in STRICT_DNS cluster per priority is not
  // guaranteed. Need a clear agreement on the behavior here, whether it is allowable to have
  // duplicated hosts inside a priority. And if we want to enforce this behavior, it should be done
  // inside the priority state manager.
  for (const ResolveTargetPtr& target : resolve_targets_) {
    priority_state_manager.initializePriorityFor(target->locality_lb_endpoints_);
    for (const HostSharedPtr& host : target->hosts_) {
      if (target->locality_lb_endpoints_.priority() == current_priority) {
        priority_state_manager.registerHostForPriority(host, target->locality_lb_endpoints_);
      }
    }
  }

  // TODO(dio): Add assertion in here.
  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt, weighted_priority_health_,
      overprovisioning_factor_);
}

StrictDnsClusterImpl::ResolveTarget::ResolveTarget(
    StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher, const std::string& dns_address,
    const uint32_t dns_port,
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint)
    : parent_(parent), locality_lb_endpoints_(locality_lb_endpoint), lb_endpoint_(lb_endpoint),
      dns_address_(dns_address),
      hostname_(lb_endpoint_.endpoint().hostname().empty() ? dns_address_
                                                           : lb_endpoint_.endpoint().hostname()),
      port_(dns_port),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })) {}

StrictDnsClusterImpl::ResolveTarget::~ResolveTarget() {
  if (active_query_) {
    active_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
}

void StrictDnsClusterImpl::ResolveTarget::startResolve() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);
  parent_.info_->configUpdateStats().update_attempt_.inc();

  active_query_ = parent_.dns_resolver_->resolve(
      dns_address_, parent_.dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
             std::list<Network::DnsResponse>&& response) -> void {
        active_query_ = nullptr;
        ENVOY_LOG(trace, "async DNS resolution complete for {} details {}", dns_address_, details);

        std::chrono::milliseconds final_refresh_rate = parent_.dns_refresh_rate_ms_;

        if (status == Network::DnsResolver::ResolutionStatus::Success) {
          parent_.info_->configUpdateStats().update_success_.inc();

          HostVector new_hosts;
          std::chrono::seconds ttl_refresh_rate = std::chrono::seconds::max();
          absl::flat_hash_set<std::string> all_new_hosts;
          for (const auto& resp : response) {
            const auto& addrinfo = resp.addrInfo();
            // TODO(mattklein123): Currently the DNS interface does not consider port. We need to
            // make a new address that has port in it. We need to both support IPv6 as well as
            // potentially move port handling into the DNS interface itself, which would work better
            // for SRV.
            ASSERT(addrinfo.address_ != nullptr);
            auto address = Network::Utility::getAddressWithPort(*(addrinfo.address_), port_);
            if (all_new_hosts.count(address->asString()) > 0) {
              continue;
            }

            new_hosts.emplace_back(new HostImpl(
                parent_.info_, hostname_, address,
                // TODO(zyfjeff): Created through metadata shared pool
                std::make_shared<const envoy::config::core::v3::Metadata>(lb_endpoint_.metadata()),
                std::make_shared<const envoy::config::core::v3::Metadata>(
                    locality_lb_endpoints_.metadata()),
                lb_endpoint_.load_balancing_weight().value(), locality_lb_endpoints_.locality(),
                lb_endpoint_.endpoint().health_check_config(), locality_lb_endpoints_.priority(),
                lb_endpoint_.health_status(), parent_.time_source_));
            all_new_hosts.emplace(address->asString());
            ttl_refresh_rate = min(ttl_refresh_rate, addrinfo.ttl_);
          }

          HostVector hosts_added;
          HostVector hosts_removed;
          if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed,
                                            all_hosts_, all_new_hosts)) {
            ENVOY_LOG(debug, "DNS hosts have changed for {}", dns_address_);
            ASSERT(std::all_of(hosts_.begin(), hosts_.end(), [&](const auto& host) {
              return host->priority() == locality_lb_endpoints_.priority();
            }));

            // Update host map for current resolve target.
            for (const auto& host : hosts_removed) {
              all_hosts_.erase(host->address()->asString());
            }
            for (const auto& host : hosts_added) {
              all_hosts_.insert({host->address()->asString(), host});
            }

            parent_.updateAllHosts(hosts_added, hosts_removed, locality_lb_endpoints_.priority());
          } else {
            parent_.info_->configUpdateStats().update_no_rebuild_.inc();
          }

          // reset failure backoff strategy because there was a success.
          parent_.failure_backoff_strategy_->reset();

          if (!response.empty() && parent_.respect_dns_ttl_ &&
              ttl_refresh_rate != std::chrono::seconds(0)) {
            final_refresh_rate = ttl_refresh_rate;
            ASSERT(ttl_refresh_rate != std::chrono::seconds::max() &&
                   final_refresh_rate.count() > 0);
          }
          ENVOY_LOG(debug, "DNS refresh rate reset for {}, refresh rate {} ms", dns_address_,
                    final_refresh_rate.count());
        } else {
          parent_.info_->configUpdateStats().update_failure_.inc();

          final_refresh_rate =
              std::chrono::milliseconds(parent_.failure_backoff_strategy_->nextBackOffMs());
          ENVOY_LOG(debug, "DNS refresh rate reset for {}, (failure) refresh rate {} ms",
                    dns_address_, final_refresh_rate.count());
        }

        // If there is an initialize callback, fire it now. Note that if the cluster refers to
        // multiple DNS names, this will return initialized after a single DNS resolution
        // completes. This is not perfect but is easier to code and unclear if the extra
        // complexity is needed so will start with this.
        parent_.onPreInitComplete();
        resolve_timer_->enableTimer(final_refresh_rate);
      });
}

absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
StrictDnsClusterFactory::createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                                           ClusterFactoryContext& context) {
  auto dns_resolver_or_error = selectDnsResolver(cluster, context);
  THROW_IF_NOT_OK(dns_resolver_or_error.status());

  return std::make_pair(std::make_shared<StrictDnsClusterImpl>(
                            cluster, context, std::move(dns_resolver_or_error.value())),
                        nullptr);
}

/**
 * Static registration for the strict dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(StrictDnsClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy
