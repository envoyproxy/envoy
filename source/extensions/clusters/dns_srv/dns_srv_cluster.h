#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dns_srv/v3/dns_srv_cluster.pb.h"
#include "envoy/extensions/clusters/dns_srv/v3/dns_srv_cluster.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/common/empty_string.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/common/logical_host.h"

namespace Envoy {
namespace Upstream {

class DnsSrvClusterFactory;
class DnsSrvClusterTest;

/**
 * TODO: add description
 */
class DnsSrvCluster : public BaseDynamicClusterImpl {
public:
  ~DnsSrvCluster() override;

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

protected:
  DnsSrvCluster(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig& dns_srv_cluster,
      ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver,
      absl::Status& creation_status);

private:
  friend class DnsSrvClusterFactory;
  friend class DnsSrvClusterTest;

  // const envoy::config::endpoint::v3::LocalityLbEndpoints& localityLbEndpoint() const {
  //   // This is checked in the constructor, i.e. at config load time.
  //   ASSERT(load_assignment_.endpoints().size() == 1);
  //   return load_assignment_.endpoints()[0];
  // }

  // const envoy::config::endpoint::v3::LbEndpoint& lbEndpoint() const {
  //   // This is checked in the constructor, i.e. at config load time.
  //   ASSERT(localityLbEndpoint().lb_endpoints().size() == 1);
  //   return localityLbEndpoint().lb_endpoints()[0];
  // }

  void startResolve();

  // ClusterImplBase
  void startPreInit() override;

  const envoy::config::cluster::v3::Cluster& cluster_;
  Network::DnsResolverSharedPtr dns_resolver_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  const bool respect_dns_ttl_;
  Event::TimerPtr resolve_timer_;
  std::string dns_address_;
  uint32_t dns_port_;
  Network::Address::InstanceConstSharedPtr current_resolved_address_;
  std::vector<Network::Address::InstanceConstSharedPtr> current_resolved_address_list_;
  LogicalHostSharedPtr logical_host_;
  Network::ActiveDnsQuery* active_dns_query_{};
  // const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoints_;
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_;
  const LocalInfo::LocalInfo& local_info_;
  const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig dns_srv_cluster_;
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
