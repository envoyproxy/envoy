#include "common/upstream/strict_dns_cluster.h"

namespace Envoy {
namespace Upstream {

StrictDnsClusterImpl::StrictDnsClusterImpl(
    const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
    Network::DnsResolverSharedPtr dns_resolver,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                             added_via_api),
      local_info_(factory_context.localInfo()), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      respect_dns_ttl_(cluster.respect_dns_ttl()) {
  std::list<ResolveTargetPtr> resolve_targets;
  const envoy::api::v2::ClusterLoadAssignment load_assignment(
      cluster.has_load_assignment() ? cluster.load_assignment()
                                    : Config::Utility::translateClusterHosts(cluster.hosts()));
  const auto& locality_lb_endpoints = load_assignment.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& socket_address = lb_endpoint.endpoint().address().socket_address();
      const std::string& url = Network::Utility::urlFromSocketAddress(socket_address, "STRICT_DNS");
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
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint)
    : parent_(parent), dns_address_(Network::Utility::hostFromTcpUrl(url)),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })),
      locality_lb_endpoint_(locality_lb_endpoint), lb_endpoint_(lb_endpoint) {
  if (absl::EndsWithIgnoreCase(url, ":srv")) {
    srv_ = true;
  } else {
    port_ = Network::Utility::portFromTcpUrl(url);
  }
}

StrictDnsClusterImpl::ResolveTarget::~ResolveTarget() {
  if (active_query_) {
    active_query_->cancel();
  }
}

void StrictDnsClusterImpl::ResolveTarget::startResolve() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);
  parent_.info_->stats().update_attempt_.inc();

  if (srv_) {
    active_query_ = parent_.dns_resolver_->resolveSrv(
        dns_address_, parent_.dns_lookup_family_,
        [this](std::list<Network::DnsSrvResponse>&& response) -> void {
          updateHosts(std::move(response),
                      static_cast<std::function<Network::Address::InstanceConstSharedPtr(
                          const Network::DnsSrvResponse&)>>(
                          [](const Network::DnsSrvResponse& dns_srv_response) {
                            return dns_srv_response.address_->address();
                          }));
        });
  } else {
    active_query_ = parent_.dns_resolver_->resolve(
        dns_address_, parent_.dns_lookup_family_,
        [this](std::list<Network::DnsResponse>&& response) -> void {
          updateHosts(
              std::move(response),
              static_cast<std::function<Network::Address::InstanceConstSharedPtr(
                  const Network::DnsResponse&)>>([this](const Network::DnsResponse& dns_response) {
                return Network::Utility::getAddressWithPort(*dns_response.address_, port_);
              }));
        });
  }
}

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
StrictDnsClusterFactory::createClusterImpl(
    const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
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
