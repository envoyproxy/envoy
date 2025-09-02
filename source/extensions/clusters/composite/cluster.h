#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

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

  Upstream::ClusterManager& cluster_manager_;
  const ClusterSetConstSharedPtr clusters_;
  const envoy::extensions::clusters::composite::v3::ClusterConfig::OverflowOption overflow_option_;

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

// Load balancer context wrapper for composite cluster.
class CompositeLoadBalancerContext : public Upstream::LoadBalancerContext {
public:
  CompositeLoadBalancerContext(Upstream::LoadBalancerContext* base_context)
      : base_context_(base_context) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override {
    return base_context_ ? base_context_->computeHashKey() : absl::nullopt;
  }

  const Network::Connection* downstreamConnection() const override {
    return base_context_ ? base_context_->downstreamConnection() : nullptr;
  }

  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    return base_context_ ? base_context_->metadataMatchCriteria() : nullptr;
  }

  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return base_context_ ? base_context_->downstreamHeaders() : nullptr;
  }

  StreamInfo::StreamInfo* requestStreamInfo() const override {
    return base_context_ ? base_context_->requestStreamInfo() : nullptr;
  }

  uint32_t hostSelectionRetryCount() const override {
    return base_context_ ? base_context_->hostSelectionRetryCount() : 0;
  }

  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return base_context_ ? base_context_->upstreamSocketOptions() : nullptr;
  }

  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return base_context_ ? base_context_->upstreamTransportSocketOptions() : nullptr;
  }

  absl::optional<std::pair<absl::string_view, bool>> overrideHostToSelect() const override {
    return base_context_ ? base_context_->overrideHostToSelect() : absl::nullopt;
  }

  void setHeadersModifier(std::function<void(Http::ResponseHeaderMap&)> headers_modifier) override {
    if (base_context_) {
      base_context_->setHeadersModifier(std::move(headers_modifier));
    }
  }

  const Upstream::HealthyAndDegradedLoad& determinePriorityLoad(
      const Upstream::PrioritySet& priority_set,
      const Upstream::HealthyAndDegradedLoad& original_priority_load,
      const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) override {
    ASSERT(base_context_ != nullptr);
    return base_context_->determinePriorityLoad(priority_set, original_priority_load,
                                                priority_mapping_func);
  }

  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return base_context_ ? base_context_->shouldSelectAnotherHost(host) : false;
  }

  void onAsyncHostSelection(Upstream::HostConstSharedPtr&& host, std::string&& details) override {
    if (base_context_) {
      base_context_->onAsyncHostSelection(std::move(host), std::move(details));
    }
  }

private:
  Upstream::LoadBalancerContext* base_context_;
};

// Load balancer used by each worker thread. It will be refreshed when clusters, hosts or priorities
// are updated.
class CompositeClusterLoadBalancer : public Upstream::LoadBalancer,
                                     public Upstream::ClusterUpdateCallbacks,
                                     protected Logger::Loggable<Logger::Id::upstream> {
public:
  CompositeClusterLoadBalancer(
      const Upstream::ClusterInfoConstSharedPtr& parent_info,
      Upstream::ClusterManager& cluster_manager, const ClusterSetConstSharedPtr& clusters,
      envoy::extensions::clusters::composite::v3::ClusterConfig::OverflowOption overflow_option);

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                           std::vector<uint8_t>& hash_key) override;

  // Simple implementation - no complex callback aggregation needed.
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }

public:
  // Test-only access methods
  uint32_t getRetryAttemptCount(Upstream::LoadBalancerContext* context) const;
  absl::optional<size_t> mapRetryAttemptToClusterIndex(uint32_t retry_attempt) const;

private:
  Upstream::ThreadLocalCluster* getClusterByIndex(size_t cluster_index) const;
  void addMemberUpdateCallbackForCluster(Upstream::ThreadLocalCluster& thread_local_cluster);
  void refresh(OptRef<const std::string> excluded_cluster = OptRef<const std::string>());

  const Upstream::ClusterInfoConstSharedPtr parent_info_;
  Upstream::ClusterManager& cluster_manager_;
  const ClusterSetConstSharedPtr clusters_;
  const envoy::extensions::clusters::composite::v3::ClusterConfig::OverflowOption overflow_option_;
  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
  absl::flat_hash_map<std::string, Envoy::Common::CallbackHandlePtr> member_update_cbs_;
};

class CompositeLoadBalancerFactory : public Upstream::LoadBalancerFactory {
public:
  CompositeLoadBalancerFactory(const Cluster& cluster) : cluster_(cluster) {}

  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override;

private:
  const Cluster& cluster_;
};

struct CompositeThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  CompositeThreadAwareLoadBalancer(const Cluster& cluster)
      : factory_(std::make_shared<CompositeLoadBalancerFactory>(cluster)) {}

  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

private:
  std::shared_ptr<CompositeLoadBalancerFactory> factory_;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::composite::v3::ClusterConfig> {
public:
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.composite") {}

private:
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
