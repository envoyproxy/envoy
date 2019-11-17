#pragma once

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of Upstream::Cluster that does periodic DNS resolution and updates the host
 * member set if the DNS members change.
 */
class StrictDnsClusterImpl : public BaseDynamicClusterImpl {
public:
  StrictDnsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                       Network::DnsResolverSharedPtr dns_resolver,
                       Server::Configuration::TransportSocketFactoryContext& factory_context,
                       Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct ResolveTarget {
    ResolveTarget(StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher,
                  const std::string& url,
                  const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
                  const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint);
    ~ResolveTarget();
    void startResolve();

    StrictDnsClusterImpl& parent_;
    Network::ActiveDnsQuery* active_query_{};
    std::string dns_address_;
    uint32_t port_;
    Event::TimerPtr resolve_timer_;
    HostVector hosts_;
    const envoy::api::v2::endpoint::LocalityLbEndpoints locality_lb_endpoint_;
    const envoy::api::v2::endpoint::LbEndpoint lb_endpoint_;
    HostMap all_hosts_;
  };

  using ResolveTargetPtr = std::unique_ptr<ResolveTarget>;

  void updateAllHosts(const HostVector& hosts_added, const HostVector& hosts_removed,
                      uint32_t priority);

  // ClusterImplBase
  void startPreInit() override;

  const LocalInfo::LocalInfo& local_info_;
  Network::DnsResolverSharedPtr dns_resolver_;
  std::list<ResolveTargetPtr> resolve_targets_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  BackOffStrategyPtr failure_backoff_strategy_;
  const bool respect_dns_ttl_;
  Network::DnsLookupFamily dns_lookup_family_;
  uint32_t overprovisioning_factor_;
};

/**
 * Factory for StrictDnsClusterImpl
 */
class StrictDnsClusterFactory : public ClusterFactoryImplBase {
public:
  StrictDnsClusterFactory()
      : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().StrictDns) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
  createClusterImpl(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
                    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                    Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy
