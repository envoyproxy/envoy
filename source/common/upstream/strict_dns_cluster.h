#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of Upstream::Cluster that does periodic DNS resolution and updates the host
 * member set if the DNS members change.
 */
class StrictDnsClusterImpl : public BaseDynamicClusterImpl {
public:
  StrictDnsClusterImpl(const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
                       Network::DnsResolverSharedPtr dns_resolver,
                       Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                       Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct ResolveTarget {
    ResolveTarget(StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher,
                  const std::string& url,
                  const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
                  const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint);
    ~ResolveTarget();
    void startResolve();

    StrictDnsClusterImpl& parent_;
    Network::ActiveDnsQuery* active_query_{};
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoints_;
    const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint_;
    const std::string dns_address_;
    const std::string hostname_;
    const uint32_t port_;
    const Event::TimerPtr resolve_timer_;
    HostVector hosts_;
    HostMap all_hosts_;
  };

  using ResolveTargetPtr = std::unique_ptr<ResolveTarget>;

  void updateAllHosts(const HostVector& hosts_added, const HostVector& hosts_removed,
                      uint32_t priority);

  // ClusterImplBase
  void startPreInit() override;

  // Keep load assignment as a member to make sure its data referenced in
  // resolve_targets_ outlives them.
  const envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_;
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
  StrictDnsClusterFactory() : ClusterFactoryImplBase("envoy.cluster.strict_dns") {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy
