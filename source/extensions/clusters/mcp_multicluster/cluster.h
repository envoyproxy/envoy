#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/mcp_multicluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/mcp_multicluster/v3/cluster.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/cluster_factory.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/composite/cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace McpMulticluster {

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig> {
public:
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.mcp_multicluster") {}

private:
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;

  Composite::ClusterFactory composite_cluster_factory_;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace McpMulticluster
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
