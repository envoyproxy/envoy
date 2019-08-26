#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class AggregateLoadBalancerContext : public Upstream::LoadBalancerContext {
public:
  AggregateLoadBalancerContext(Upstream::LoadBalancerContext* context,
                               Upstream::LoadBalancerBase::HostAvailability host_avail)
      : host_health_(hostHealthType(host_avail)), context_(context) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override {
    if (context_) {
      return context_->computeHashKey();
    }
    return {};
  }
  const Network::Connection* downstreamConnection() const override {
    if (context_) {
      return context_->downstreamConnection();
    }
    return nullptr;
  }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    if (context_) {
      return context_->metadataMatchCriteria();
    }
    return nullptr;
  }
  const Http::HeaderMap* downstreamHeaders() const override {
    if (context_) {
      return context_->downstreamHeaders();
    }
    return nullptr;
  }
  const Upstream::HealthyAndDegradedLoad&
  determinePriorityLoad(const Upstream::PrioritySet& priority_set,
                        const Upstream::HealthyAndDegradedLoad& original_priority_load) override {
    if (context_) {
      return context_->determinePriorityLoad(priority_set, original_priority_load);
    }
    return original_priority_load;
  }
  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    if (context_) {
      return context_->shouldSelectAnotherHost(host) || host.health() != host_health_;
    }
    return host.health() != host_health_;
  }
  uint32_t hostSelectionRetryCount() const override {
    if (context_) {
      return context_->hostSelectionRetryCount();
    }
    return 1;
  }
  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    if (context_) {
      return context_->upstreamSocketOptions();
    }
    return {};
  }

private:
  Upstream::Host::Health
  hostHealthType(Upstream::LoadBalancerBase::HostAvailability host_availability) {
    switch (host_availability) {
    case Upstream::LoadBalancerBase::HostAvailability::Healthy:
      return Upstream::Host::Health::Healthy;
    case Upstream::LoadBalancerBase::HostAvailability::Degraded:
      return Upstream::Host::Health::Degraded;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  Upstream::Host::Health host_health_;
  Upstream::LoadBalancerContext* context_;
};

class AggregateClusterLoadBalancer : public Upstream::LoadBalancer,
                                     Upstream::ClusterUpdateCallbacks {
public:
  AggregateClusterLoadBalancer(Upstream::ClusterManager& cluster_manager,
                               const std::vector<std::string>& clusters,
                               Upstream::ClusterStats& stats, Runtime::Loader& runtime,
                               Runtime::RandomGenerator& random,
                               const envoy::api::v2::Cluster::CommonLbConfig& common_config);

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override {
    return load_balancer_->chooseHost(context);
  }

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

private:
  void refreshLoadBalancer();

  class LoadBalancerImpl : public Upstream::LoadBalancerBase {
  public:
    LoadBalancerImpl(AggregateClusterLoadBalancer& parent,
                     const Upstream::PrioritySetImpl& priority_set,
                     std::vector<Upstream::ThreadLocalCluster*>&& priority_to_cluster)
        : Upstream::LoadBalancerBase(priority_set, parent.stats_, parent.runtime_, parent.random_,
                                     parent.common_config_),
          priority_to_cluster_(std::move(priority_to_cluster)) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

    // Upstream::LoadBalancerBase
    Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext*) override {
      // The aggregate load balancer has implemented chooseHost, return nullptr directly.
      return nullptr;
    }

  private:
    std::vector<Upstream::ThreadLocalCluster*> priority_to_cluster_;
  };

  using LoadBalancerPtr = std::unique_ptr<LoadBalancerImpl>;

  LoadBalancerPtr load_balancer_;
  Upstream::ClusterManager& cluster_manager_;
  std::vector<std::string> clusters_;
  Upstream::ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const envoy::api::v2::Cluster::CommonLbConfig& common_config_;
};

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy