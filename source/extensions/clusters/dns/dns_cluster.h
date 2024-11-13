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

DECLARE_FACTORY(DnsClusterFactory);

} // namespace Upstream
} // namespace Envoy
