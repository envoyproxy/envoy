#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

class StaticClusterFactory;
class UpstreamImplTestBase;
/**
 * Implementation of Upstream::Cluster for static clusters (clusters that have a fixed number of
 * hosts with resolved IP addresses).
 */
class StaticClusterImpl : public ClusterImplBase {
public:
  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

protected:
  StaticClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context, absl::Status& creation_status);

private:
  friend class StaticClusterFactory;
  friend class UpstreamImplTestBase;

  // ClusterImplBase
  void startPreInit() override;

  PriorityStateManagerPtr priority_state_manager_;
  uint32_t overprovisioning_factor_;
  bool weighted_priority_health_;
};

/**
 * Factory for StaticClusterImpl cluster.
 */
class StaticClusterFactory : public ClusterFactoryImplBase {
public:
  StaticClusterFactory() : ClusterFactoryImplBase("envoy.cluster.static") {}

  absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
  createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(StaticClusterFactory);

} // namespace Upstream
} // namespace Envoy
