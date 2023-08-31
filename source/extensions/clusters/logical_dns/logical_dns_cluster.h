#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/empty_string.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/common/logical_host.h"

namespace Envoy {
namespace Upstream {

class LogicalDnsClusterFactory;
class LogicalDnsClusterTest;

/**
 * The LogicalDnsCluster is a type of cluster that creates a single logical host that wraps
 * an async DNS resolver. The DNS resolver will continuously resolve, and swap in the first IP
 * address in the resolution set. However the logical owning host will not change. Any created
 * connections against this host will use the currently resolved IP. This means that a connection
 * pool using the logical host may end up with connections to many different real IPs.
 *
 * This cluster type is useful for large web services that use DNS in a round robin fashion, such
 * that DNS resolution may repeatedly return different results. A single connection pool can be
 * created that will internally have connections to different backends, while still allowing long
 * connection lengths and keep alive. The cluster type should only be used when an IP address change
 * means that connections using the IP should not drain.
 */
class LogicalDnsCluster : public ClusterImplBase {
public:
  ~LogicalDnsCluster() override;

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  friend class LogicalDnsClusterFactory;
  friend class LogicalDnsClusterTest;

  LogicalDnsCluster(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver);

  const envoy::config::endpoint::v3::LocalityLbEndpoints& localityLbEndpoint() const {
    // This is checked in the constructor, i.e. at config load time.
    ASSERT(load_assignment_.endpoints().size() == 1);
    return load_assignment_.endpoints()[0];
  }

  const envoy::config::endpoint::v3::LbEndpoint& lbEndpoint() const {
    // This is checked in the constructor, i.e. at config load time.
    ASSERT(localityLbEndpoint().lb_endpoints().size() == 1);
    return localityLbEndpoint().lb_endpoints()[0];
  }

  void startResolve();

  // ClusterImplBase
  void startPreInit() override;

  Network::DnsResolverSharedPtr dns_resolver_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  BackOffStrategyPtr failure_backoff_strategy_;
  const bool respect_dns_ttl_;
  Network::DnsLookupFamily dns_lookup_family_;
  Event::TimerPtr resolve_timer_;
  std::string dns_address_;
  uint32_t dns_port_;
  std::string hostname_;
  Network::Address::InstanceConstSharedPtr current_resolved_address_;
  std::vector<Network::Address::InstanceConstSharedPtr> current_resolved_address_list_;
  LogicalHostSharedPtr logical_host_;
  Network::ActiveDnsQuery* active_dns_query_{};
  const LocalInfo::LocalInfo& local_info_;
  const envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_;
};

class LogicalDnsClusterFactory : public ClusterFactoryImplBase {
public:
  LogicalDnsClusterFactory() : ClusterFactoryImplBase("envoy.cluster.logical_dns") {}

private:
  friend class LogicalDnsClusterTest;
  absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
  createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(LogicalDnsClusterFactory);

} // namespace Upstream
} // namespace Envoy
