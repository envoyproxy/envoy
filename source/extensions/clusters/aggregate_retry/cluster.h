#pragma once

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate_retry/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate_retry/v3/cluster.pb.validate.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/aggregate_retry/lb_context.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace AggregateRetry {

// Order matters so a vector must be used for rebuilds. If the vector size becomes larger we can
// maintain a parallel set for lookups during cluster update callbacks.
using ClusterSet = std::vector<std::string>;
using ClusterSetConstSharedPtr = std::shared_ptr<const ClusterSet>;

class Cluster : public Upstream::ClusterImplBase {
public:
  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Secondary;
  }

  // Getters that return the values from ClusterImplBase.
  Runtime::Loader& runtime() const { return runtime_; }
  Random::RandomGenerator& random() const { return random_; }

  Upstream::ClusterManager& cluster_manager_;
  const ClusterSetConstSharedPtr clusters_;
  const envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::RetryOverflowBehavior
      retry_overflow_behavior_;

protected:
  Cluster(const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig& config,
          Upstream::ClusterFactoryContext& context, absl::Status& creation_status);

private:
  friend class ClusterFactory;
  friend class AggregateRetryClusterTest;

  // Upstream::ClusterImplBase
  void startPreInit() override { onPreInitComplete(); }
};

// Load balancer used by each worker thread. It will be refreshed when clusters, hosts or priorities
// are updated.
class AggregateRetryClusterLoadBalancer : public Upstream::LoadBalancer,
                                          Upstream::ClusterUpdateCallbacks,
                                          Logger::Loggable<Logger::Id::upstream> {
public:
  friend class AggregateRetryLoadBalancerFactory;
  AggregateRetryClusterLoadBalancer(
      const Upstream::ClusterInfoConstSharedPtr& parent_info,
      Upstream::ClusterManager& cluster_manager, const ClusterSetConstSharedPtr& clusters,
      envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::RetryOverflowBehavior
          retry_overflow_behavior);

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                           const Upstream::Host& /*host*/,
                           std::vector<uint8_t>& /*hash_key*/) override;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;

  // Extract retry attempt count from LoadBalancerContext.
  uint32_t getRetryAttemptCount(Upstream::LoadBalancerContext* context) const;

  // Map retry attempt to cluster index based on configuration.
  size_t mapRetryAttemptToClusterIndex(uint32_t retry_attempt) const;

  // Get cluster by index, handling overflow behavior.
  Upstream::ThreadLocalCluster* getClusterByIndex(size_t cluster_index) const;

private:
  void addMemberUpdateCallbackForCluster(Upstream::ThreadLocalCluster& thread_local_cluster);
  void refresh(OptRef<const std::string> excluded_cluster = OptRef<const std::string>());

  Upstream::ClusterInfoConstSharedPtr parent_info_;
  Upstream::ClusterManager& cluster_manager_;
  const ClusterSetConstSharedPtr clusters_;
  const envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::RetryOverflowBehavior
      retry_overflow_behavior_;
  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
  absl::flat_hash_map<std::string, Envoy::Common::CallbackHandlePtr> member_update_cbs_;
};

// Load balancer factory created by the main thread and will be called in each worker thread to
// create the thread local load balancer.
class AggregateRetryLoadBalancerFactory : public Upstream::LoadBalancerFactory {
public:
  AggregateRetryLoadBalancerFactory(const Cluster& cluster) : cluster_(cluster) {}
  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
    return std::make_unique<AggregateRetryClusterLoadBalancer>(
        cluster_.info(), cluster_.cluster_manager_, cluster_.clusters_,
        cluster_.retry_overflow_behavior_);
  }

  const Cluster& cluster_;
};

// Thread aware load balancer created by the main thread.
struct AggregateRetryThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  AggregateRetryThreadAwareLoadBalancer(const Cluster& cluster)
      : factory_(std::make_shared<AggregateRetryLoadBalancerFactory>(cluster)) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

  std::shared_ptr<AggregateRetryLoadBalancerFactory> factory_;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig> {
public:
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.aggregate_retry") {}

private:
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace AggregateRetry
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
