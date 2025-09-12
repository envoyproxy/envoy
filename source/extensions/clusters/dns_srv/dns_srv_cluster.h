#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dns_srv/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dns_srv/v3/cluster.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/common/empty_string.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

class DnsSrvClusterFactory;
class DnsSrvClusterTest;

/**
 * Cluster implementation for DNS SRV records.
 * An SRV record specifies hostname(s) for a service, each hostname additionally contains:
 * - weight
 * - priority
 * - port number
 *
 * For each hostname, we will resolve a list of IP addresses (A/AAAA records) and populate the
 * cluster endpoints.
 *
 * Current limitations:
 * - Only c-ares DNS resolver is supported.
 * - Only one SRV record is supported per cluster.
 * - All resolved IP addresses will be added to cluster (acting like Strict DNS cluster).
 * - Both initial SRV record and subsequent A/AAAA records resolved via the same DNS resolver.
 */
class DnsSrvCluster : public BaseDynamicClusterImpl {
public:
  DnsSrvCluster(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig& dns_srv_cluster,
      ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver,
      absl::Status& creation_status);

  ~DnsSrvCluster() override;

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  friend class DnsSrvClusterFactory;
  friend class DnsSrvClusterTest;

  class ResolveList;

  struct ResolveTarget {
    ResolveTarget(ResolveList& parent, Network::DnsResolverSharedPtr dns_resolver,
                  Network::DnsLookupFamily dns_lookup_family, const std::string& dns_address,
                  uint32_t priority, uint32_t weight, uint32_t dns_port);
    ~ResolveTarget();
    void startResolve();
    void addResolvedTarget(Network::Address::InstanceConstSharedPtr address);

    ResolveList& parent_;
    Network::DnsResolverSharedPtr dns_resolver_;
    Network::DnsLookupFamily dns_lookup_family_;
    const std::string srv_record_hostname_; // ResolveTarget needs to store its own copy
    const uint32_t priority_;
    const uint32_t weight_;
    const uint32_t dns_port_;
    std::list<Network::Address::InstanceConstSharedPtr> resolved_targets_;
    Network::DnsResolver::ResolutionStatus resolve_status_;
    std::string resolve_status_details_;
    Network::ActiveDnsQuery* active_dns_query_{nullptr};
  };
  using ResolveTargetPtr = std::unique_ptr<ResolveTarget>;

  // One SRV-record may return several hostnames
  // We will need to resolve all of the hostnames to IPs.
  // Potentially, there can be several IPs for each hostname.
  class ResolveList {
  public:
    ResolveList(DnsSrvCluster& parent);
    void addTarget(ResolveTargetPtr new_target);
    void noMoreTargets();
    void targetResolved(ResolveTarget* target, std::chrono::seconds dns_ttl);
    void maybeAllResolved();
    const std::list<ResolveTargetPtr>& getResolvedTargets() const;
    void addResolvedTarget(ResolveTargetPtr new_target,
                           Network::Address::InstanceConstSharedPtr resolved_address);
    std::chrono::seconds dnsTtlRefreshRate() const;

  private:
    DnsSrvCluster& parent_;
    Network::DnsResolverSharedPtr dns_resolver_;
    bool no_more_targets_{false};
    std::list<ResolveTargetPtr> active_targets_;
    std::list<ResolveTargetPtr> resolved_targets_;
    // Ideally, we should collect the list of targets and resolve each of them separately,
    // respecting their TTLs. However, this is not implemented yet, so we will just use the same TTL
    // (min) for all targets. Once this refresh rate passed, we will re-resolve the SRV and then all
    // of the targets.
    std::chrono::seconds dns_ttl_refresh_rate_;
  };
  using ResolveListPtr = std::unique_ptr<ResolveList>;

  void startResolve();
  void allTargetsResolved();

  // ClusterImplBase
  void startPreInit() override;

  const envoy::config::cluster::v3::Cluster& cluster_;
  Network::DnsResolverSharedPtr dns_resolver_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  const bool respect_dns_ttl_;
  Event::TimerPtr resolve_timer_;
  Network::ActiveDnsQuery* active_dns_query_{};
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_;
  const LocalInfo::LocalInfo& local_info_;
  const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig dns_srv_cluster_;
  const Network::DnsLookupFamily dns_lookup_family_;
  ResolveListPtr active_resolve_list_;
  // Host map for current resolve target. When we have multiple resolve targets, multiple targets
  // may contain two different hosts with the same address. This has two effects:
  // 1) This host map cannot be replaced by the cross-priority global host map in the priority
  // set.
  // 2) Cross-priority global host map may not be able to search for the expected host based on
  // the address.
  HostMap all_hosts_;
};

class DnsSrvClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                                 envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig> {
public:
  DnsSrvClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.dns_srv") {}

private:
  friend class DnsSrvClusterTest;
  absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(DnsSrvClusterFactory);

} // namespace Upstream
} // namespace Envoy
