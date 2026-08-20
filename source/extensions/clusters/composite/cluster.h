#pragma once

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/composite/lb_context.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

// Order matters so a vector must be used for rebuilds.
using ClusterSet = std::vector<std::string>;

// Immutable configuration built once on the main thread and shared by every worker load balancer.
struct CompositeClusterConfig {
  explicit CompositeClusterConfig(
      const envoy::extensions::clusters::composite::v3::ClusterConfig& config);

  // Configured cluster order, and the allowlist for metadata-supplied orders.
  ClusterSet clusters_;
  // Duplicate names map to their first index, so this resolves a name to an entry but cannot
  // recover the index of an entry already located by position.
  absl::flat_hash_map<std::string, size_t> index_by_name_;
  // When set, the dynamic metadata key holding a per-request order override.
  std::optional<Envoy::Config::MetadataKey> order_metadata_key_;
};
using CompositeClusterConfigConstSharedPtr = std::shared_ptr<const CompositeClusterConfig>;

class Cluster : public Upstream::ClusterImplBase {
public:
  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Secondary;
  }

  // Statically configured cluster order.
  const ClusterSet& clusters() const { return config_->clusters_; }

  Upstream::ClusterManager& cluster_manager_;
  const CompositeClusterConfigConstSharedPtr config_;

protected:
  Cluster(const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::composite::v3::ClusterConfig& config,
          Upstream::ClusterFactoryContext& context, absl::Status& creation_status);

private:
  friend class ClusterFactory;
  friend class CompositeClusterTest;

  // Upstream::ClusterImplBase
  void startPreInit() override { onPreInitComplete(); }
};

// Load balancer used by each worker thread.
class CompositeClusterLoadBalancer : public Upstream::LoadBalancer,
                                     Upstream::ClusterUpdateCallbacks,
                                     Logger::Loggable<Logger::Id::upstream> {
public:
  CompositeClusterLoadBalancer(const Upstream::ClusterInfoConstSharedPtr& parent_info,
                               Upstream::ClusterManager& cluster_manager,
                               const CompositeClusterConfigConstSharedPtr& config);

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) override;
  void onClusterRemoval(absl::string_view cluster_name) override;

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;
  std::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                           std::vector<uint8_t>& hash_key) override;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;

  // Extract retry attempt count from LoadBalancerContext.
  uint32_t getAttemptCount(Upstream::LoadBalancerContext* context) const;

  // `name_` points into this cluster's configuration, so it outlives the request.
  struct SelectedCluster {
    absl::string_view name_;
    size_t configured_index_{0};
    explicit operator bool() const { return !name_.empty(); }
  };

  struct ResolvedCluster {
    Upstream::ThreadLocalCluster* cluster_{nullptr};
    size_t configured_index_{0};
    explicit operator bool() const { return cluster_ != nullptr; }
  };

  // Cluster for `attempt_count`, which is 1-based: a per-request order override when
  // `order_metadata_key_` is configured and the metadata supplies a usable one, otherwise the
  // configured order. Falsy past the end of that order.
  SelectedCluster clusterForAttempt(Upstream::LoadBalancerContext* context,
                                    uint32_t attempt_count) const;

  // Falsy when the attempt has no cluster, or the named cluster is absent on this worker.
  ResolvedCluster resolveCluster(Upstream::LoadBalancerContext* context) const;

private:
  Upstream::ClusterInfoConstSharedPtr parent_info_;
  Upstream::ClusterManager& cluster_manager_;
  const CompositeClusterConfigConstSharedPtr config_;
  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
};

// Load balancer factory created by the main thread and will be called in each worker thread to
// create the thread local load balancer.
class CompositeLoadBalancerFactory : public Upstream::LoadBalancerFactory {
public:
  CompositeLoadBalancerFactory(const Cluster& cluster) : cluster_(cluster) {}

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
    return std::make_unique<CompositeClusterLoadBalancer>(
        cluster_.info(), cluster_.cluster_manager_, cluster_.config_);
  }
  bool recreateOnHostChangeDeprecated() const override { return false; }

  const Cluster& cluster_;
};

// Thread aware load balancer created by the main thread.
struct CompositeThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  CompositeThreadAwareLoadBalancer(const Cluster& cluster)
      : factory_(std::make_shared<CompositeLoadBalancerFactory>(cluster)) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

  std::shared_ptr<CompositeLoadBalancerFactory> factory_;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::composite::v3::ClusterConfig> {
public:
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.composite") {}

  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::composite::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
