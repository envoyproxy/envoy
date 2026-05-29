#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.validate.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

class LogicalDnsClusterTest;

/**
 * Factory for DnsClusterImpl
 */

class DnsClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                              envoy::extensions::clusters::dns::v3::DnsCluster> {
public:
  DnsClusterFactory() : ConfigurableClusterFactoryBase("envoy.cluster.dns") {}

private:
  friend class LogicalDnsClusterTest;
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(const envoy::config::cluster::v3::Cluster& cluster,
                          const envoy::extensions::clusters::dns::v3::DnsCluster& proto_config,
                          Upstream::ClusterFactoryContext& context) override;
};

class DnsClusterImpl : public BaseDynamicClusterImpl {
public:
  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }
  static absl::StatusOr<std::unique_ptr<DnsClusterImpl>>
  create(const envoy::config::cluster::v3::Cluster& cluster,
         const envoy::extensions::clusters::dns::v3::DnsCluster& dns_cluster,
         ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver);

protected:
  DnsClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::dns::v3::DnsCluster& dns_cluster,
                 ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver,
                 absl::Status& creation_status);

private:
  struct ResolveTarget {
    ResolveTarget(DnsClusterImpl& parent, Event::Dispatcher& dispatcher,
                  const std::string& dns_address, const uint32_t dns_port,
                  const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
                  const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint);
    ~ResolveTarget();
    void startResolve();
    bool isSuccessfulResponse(const std::list<Network::DnsResponse>& response,
                              const Network::DnsResolver::ResolutionStatus& status);

    struct ParsedHosts {
      HostVector hosts;
      std::chrono::seconds ttl_refresh_rate = std::chrono::seconds::max();
      absl::flat_hash_set<std::string> host_addresses;
    };
    absl::StatusOr<ParsedHosts>
    createLogicalDnsHosts(const std::list<Network::DnsResponse>& response);
    absl::StatusOr<ParsedHosts>
    createStrictDnsHosts(const std::list<Network::DnsResponse>& response);
    void updateLogicalDnsHosts(const std::list<Network::DnsResponse>& response,
                               const ParsedHosts& new_hosts);
    void updateStrictDnsHosts(const ParsedHosts& new_hosts);

    DnsClusterImpl& parent_;
    Network::ActiveDnsQuery* active_query_{};
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoints_;
    const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint_;
    const std::string dns_address_;
    const std::string hostname_;
    const uint32_t port_;
    const Event::TimerPtr resolve_timer_;
    HostVector hosts_;

    // Host map for current resolve target. When we have multiple resolve targets, multiple targets
    // may contain two different hosts with the same address. This has two effects:
    // 1) This host map cannot be replaced by the cross-priority global host map in the priority
    // set.
    // 2) Cross-priority global host map may not be able to search for the expected host based on
    // the address.
    HostMap all_hosts_;

    // These attributes are only used for logical DNS resolution. We use them to keep track of the
    // previous responses, so we can check and update the hosts only when the DNS response changes.
    // We cache those values here to avoid fetching them from the logical hosts, as that requires
    // synchronization mechanisms.
    Network::Address::InstanceConstSharedPtr logic_dns_cached_address_;
    std::vector<Network::Address::InstanceConstSharedPtr> logic_dns_cached_address_list_;
  };

  using ResolveTargetPtr = std::unique_ptr<ResolveTarget>;

  void updateAllHosts(const HostVector& hosts_added, const HostVector& hosts_removed,
                      uint32_t priority);

  // ClusterImplBase
  void startPreInit() override;

  static envoy::config::endpoint::v3::ClusterLoadAssignment
  extractAndProcessLoadAssignment(const envoy::config::cluster::v3::Cluster& cluster,
                                  bool all_addresses_in_single_endpoint);

  // Keep load assignment as a member to make sure its data referenced in
  // resolve_targets_ outlives them.
  const envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_;
  const LocalInfo::LocalInfo& local_info_;
  Network::DnsResolverSharedPtr dns_resolver_;
  std::list<ResolveTargetPtr> resolve_targets_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  const std::chrono::milliseconds dns_jitter_ms_;
  BackOffStrategyPtr failure_backoff_strategy_;
  const bool respect_dns_ttl_;
  Network::DnsLookupFamily dns_lookup_family_;
  uint32_t overprovisioning_factor_;
  bool weighted_priority_health_;
  bool all_addresses_in_single_endpoint_;
};

DECLARE_FACTORY(DnsClusterFactory);

} // namespace Upstream
} // namespace Envoy
