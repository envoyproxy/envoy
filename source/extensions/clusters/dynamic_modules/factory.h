#pragma once

#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.validate.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/dynamic_modules/cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

/**
 * Factory for creating DynamicModuleCluster instances.
 */
class DynamicModuleClusterFactoryTestPeer;

class DynamicModuleClusterFactory
    : public Upstream::ConfigurableClusterFactoryBase<
          envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig> {
public:
  DynamicModuleClusterFactory()
      : ConfigurableClusterFactoryBase("envoy.clusters.dynamic_modules") {}

private:
  friend class DynamicModuleClusterFactoryTestPeer;
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(DynamicModuleClusterFactory);

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
