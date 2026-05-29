#include "source/extensions/clusters/mcp_multicluster/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"
#include "envoy/extensions/clusters/mcp_multicluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/mcp_multicluster/v3/cluster.pb.validate.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace McpMulticluster {

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
ClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  envoy::config::cluster::v3::Cluster cluster_with_metadata = cluster;
  (*cluster_with_metadata.mutable_metadata()
        ->mutable_typed_filter_metadata())["envoy.clusters.mcp_multicluster"]
      .PackFrom(proto_config);
  envoy::extensions::clusters::composite::v3::ClusterConfig composite_cluster_config;

  for (const auto& server : proto_config.servers()) {
    composite_cluster_config.add_clusters()->set_name(server.mcp_cluster().cluster());
  }

  return composite_cluster_factory_.createClusterWithConfig(cluster_with_metadata,
                                                            composite_cluster_config, context);
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace McpMulticluster
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
