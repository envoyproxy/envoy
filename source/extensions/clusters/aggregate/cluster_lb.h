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
                               Upstream::LoadBalancerBase::HostAvailability host_avail,
                               uint32_t host_priority)
      : context_(context), host_avail_(host_avail), host_priority_(host_priority) {
    if (context_ == nullptr) {
      context_ = new Upstream::LoadBalancerContextBase();
      own_context_ = true;
    }
  }

  ~AggregateLoadBalancerContext() override {
    if (own_context_) {
      delete context_;
    }
  }

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return context_->computeHashKey(); }
  const Network::Connection* downstreamConnection() const override {
    return context_->downstreamConnection();
  }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    return context_->metadataMatchCriteria();
  }
  const Http::HeaderMap* downstreamHeaders() const override {
    return context_->downstreamHeaders();
  }
  const Upstream::HealthyAndDegradedLoad&
  determinePriorityLoad(const Upstream::PrioritySet&,
                        const Upstream::HealthyAndDegradedLoad& original_priority_load) override {
    priority_load_ = original_priority_load;

    // Re-assign load. Set all traffic to the priority and availability selected in aggregate
    // cluster.
    // TODO(yxue): allow determinePriorityLoad to affect the load of top level cluster and verify it
    // works with current retry plugin
    size_t priorities = priority_load_.healthy_priority_load_.get().size();
    priority_load_.healthy_priority_load_.get().assign(priorities, 0);
    priority_load_.degraded_priority_load_.get().assign(priorities, 0);

    if (host_avail_ == Upstream::LoadBalancerBase::HostAvailability::Healthy) {
      priority_load_.healthy_priority_load_.get()[host_priority_] = 100;
    } else {
      priority_load_.degraded_priority_load_.get()[host_priority_] = 100;
    }
    return priority_load_;
  }
  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return context_->shouldSelectAnotherHost(host);
  }
  uint32_t hostSelectionRetryCount() const override { return context_->hostSelectionRetryCount(); }
  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return context_->upstreamSocketOptions();
  }

private:
  Upstream::HealthyAndDegradedLoad priority_load_;
  Upstream::LoadBalancerContext* context_{nullptr};
  Upstream::LoadBalancerBase::HostAvailability host_avail_;
  uint32_t host_priority_;
  bool own_context_{false};
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
    // Initialize the inner load balancer.
    if (!initialized_) {
      initialize();
    }

    if (load_balancer_ != nullptr) {
      return load_balancer_->chooseHost(context);
    }
    return nullptr;
  }

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

private:
  void refresh();
  void refresh(const std::vector<std::string>& clusters);
  void initialize();

  class LoadBalancerImpl : public Upstream::LoadBalancerBase {
  public:
    LoadBalancerImpl(AggregateClusterLoadBalancer& parent,
                     std::pair<Upstream::PrioritySetImpl,
                               std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>&&
                         priority_setting)
        : Upstream::LoadBalancerBase(priority_setting.first, parent.stats_, parent.runtime_,
                                     parent.random_, parent.common_config_),
          priority_to_cluster_(std::move(priority_setting.second)) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

    // Upstream::LoadBalancerBase
    Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext*) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE
    }

  private:
    std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>> priority_to_cluster_;
  };

  using LoadBalancerPtr = std::unique_ptr<LoadBalancerImpl>;

  bool initialized_{false};
  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
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