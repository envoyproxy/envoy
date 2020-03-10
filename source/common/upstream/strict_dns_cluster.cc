#include "common/upstream/strict_dns_cluster.h"

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

namespace Envoy {
namespace Upstream {

StrictDnsClusterImpl::StrictDnsClusterImpl(
    const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
    Network::DnsResolverSharedPtr dns_resolver,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                             added_via_api),
      local_info_(factory_context.localInfo()), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      respect_dns_ttl_(cluster.respect_dns_ttl()) {
  failure_backoff_strategy_ =
      Config::Utility::prepareDnsRefreshStrategy<envoy::config::cluster::v3::Cluster>(
          cluster, dns_refresh_rate_ms_.count(), factory_context.random());

  std::list<ResolveTargetPtr> resolve_targets;
  const envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment(
      cluster.has_load_assignment()
          ? cluster.load_assignment()
          : Config::Utility::translateClusterHosts(cluster.hidden_envoy_deprecated_hosts()));
  const auto& locality_lb_endpoints = load_assignment.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    validateEndpointsForZoneAwareRouting(locality_lb_endpoint);

    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& socket_address = lb_endpoint.endpoint().address().socket_address();
      if (!socket_address.resolver_name().empty()) {
        throw EnvoyException("STRICT_DNS clusters must NOT have a custom resolver name set");
      }

      const std::string& url =
          fmt::format("tcp://{}:{}", socket_address.address(), socket_address.port_value());
      resolve_targets.emplace_back(new ResolveTarget(*this, factory_context.dispatcher(), url,
                                                     locality_lb_endpoint, lb_endpoint));
    }
  }
  resolve_targets_ = std::move(resolve_targets);
  dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);

  overprovisioning_factor_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      load_assignment.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);
}

void StrictDnsClusterImpl::startPreInit() {
  for (const ResolveTargetPtr& target : resolve_targets_) {
    target->startResolve();
  }
}

void StrictDnsClusterImpl::updateAllHosts(const HostVector& hosts_added,
                                          const HostVector& hosts_removed,
                                          uint32_t current_priority) {
  PriorityStateManager priority_state_manager(*this, local_info_, nullptr);
  // At this point we know that we are different so make a new host list and notify.
  //
  // TODO(dio): The uniqueness of a host address resolved in STRICT_DNS cluster per priority is not
  // guaranteed. Need a clear agreement on the behavior here, whether it is allowable to have
  // duplicated hosts inside a priority. And if we want to enforce this behavior, it should be done
  // inside the priority state manager.
  for (const ResolveTargetPtr& target : resolve_targets_) {
    priority_state_manager.initializePriorityFor(target->locality_lb_endpoint_);
    for (const HostSharedPtr& host : target->hosts_) {
      if (target->locality_lb_endpoint_.priority() == current_priority) {
        priority_state_manager.registerHostForPriority(host, target->locality_lb_endpoint_);
      }
    }
  }

  // TODO(dio): Add assertion in here.
  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt, overprovisioning_factor_);
}

StrictDnsClusterImpl::ResolveTarget::ResolveTarget(
    StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher, const std::string& url,
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint)
    : parent_(parent), dns_address_(Network::Utility::hostFromTcpUrl(url)),
      port_(Network::Utility::portFromTcpUrl(url)),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })),
      locality_lb_endpoint_(locality_lb_endpoint), lb_endpoint_(lb_endpoint) {}

StrictDnsClusterImpl::ResolveTarget::~ResolveTarget() {
  if (active_query_) {
    active_query_->cancel();
  }
}

void StrictDnsClusterImpl::ResolveTarget::startResolve() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);
  parent_.info_->stats().update_attempt_.inc();

  active_query_ = parent_.dns_resolver_->resolve(
      dns_address_, parent_.dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status,
             std::list<Network::DnsResponse>&& response) -> void {
        active_query_ = nullptr;
        ENVOY_LOG(trace, "async DNS resolution complete for {}", dns_address_);

        std::chrono::milliseconds final_refresh_rate = parent_.dns_refresh_rate_ms_;

        if (status == Network::DnsResolver::ResolutionStatus::Success) {
          parent_.info_->stats().update_success_.inc();

          std::unordered_map<std::string, HostSharedPtr> updated_hosts;
          HostVector new_hosts;
          std::chrono::seconds ttl_refresh_rate = std::chrono::seconds::max();
          for (const auto& resp : response) {
            // TODO(mattklein123): Currently the DNS interface does not consider port. We need to
            // make a new address that has port in it. We need to both support IPv6 as well as
            // potentially move port handling into the DNS interface itself, which would work better
            // for SRV.
            ASSERT(resp.address_ != nullptr);
            new_hosts.emplace_back(new HostImpl(
                parent_.info_, dns_address_,
                Network::Utility::getAddressWithPort(*(resp.address_), port_),
                // TODO(zyfjeff): Created through metadata shared pool
                std::make_shared<const envoy::config::core::v3::Metadata>(lb_endpoint_.metadata()),
                lb_endpoint_.load_balancing_weight().value(), locality_lb_endpoint_.locality(),
                lb_endpoint_.endpoint().health_check_config(), locality_lb_endpoint_.priority(),
                lb_endpoint_.health_status()));

            ttl_refresh_rate = min(ttl_refresh_rate, resp.ttl_);
          }

          HostVector hosts_added;
          HostVector hosts_removed;
          if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed,
                                            updated_hosts, all_hosts_)) {
            ENVOY_LOG(debug, "DNS hosts have changed for {}", dns_address_);
            ASSERT(std::all_of(hosts_.begin(), hosts_.end(), [&](const auto& host) {
              return host->priority() == locality_lb_endpoint_.priority();
            }));
            parent_.updateAllHosts(hosts_added, hosts_removed, locality_lb_endpoint_.priority());
          } else {
            parent_.info_->stats().update_no_rebuild_.inc();
          }

          all_hosts_ = std::move(updated_hosts);

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
          parent_.info_->stats().update_failure_.inc();

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

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
StrictDnsClusterFactory::createClusterImpl(
    const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  auto selected_dns_resolver = selectDnsResolver(cluster, context);

  return std::make_pair(std::make_shared<StrictDnsClusterImpl>(
                            cluster, context.runtime(), selected_dns_resolver,
                            socket_factory_context, std::move(stats_scope), context.addedViaApi()),
                        nullptr);
}

/**
 * Static registration for the strict dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(StrictDnsClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy
