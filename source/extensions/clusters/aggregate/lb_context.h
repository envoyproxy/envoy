#pragma once

#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

// AggregateLoadBalancerContext wraps the load balancer context to re-assign priority load
// according the to host priority selected by the aggregate load balancer.
class AggregateLoadBalancerContext : public Upstream::LoadBalancerContext {
public:
  AggregateLoadBalancerContext(Upstream::LoadBalancerContext* context,
                               Upstream::LoadBalancerBase::HostAvailability host_availability,
                               uint32_t host_priority)
      : host_availability_(host_availability), host_priority_(host_priority) {
    if (context == nullptr) {
      owned_context_ = std::make_unique<Upstream::LoadBalancerContextBase>();
      context_ = owned_context_.get();
    } else {
      context_ = context;
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
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return context_->downstreamHeaders();
  }
  const Upstream::HealthyAndDegradedLoad&
  determinePriorityLoad(const Upstream::PrioritySet&,
                        const Upstream::HealthyAndDegradedLoad& original_priority_load) override {
    // Re-assign load. Set all traffic to the priority and availability selected in aggregate
    // cluster.
    // TODO(yxue): allow determinePriorityLoad to affect the load of top level cluster and verify it
    // works with current retry plugin
    const size_t priorities = original_priority_load.healthy_priority_load_.get().size();
    priority_load_.healthy_priority_load_.get().assign(priorities, 0);
    priority_load_.degraded_priority_load_.get().assign(priorities, 0);

    if (host_availability_ == Upstream::LoadBalancerBase::HostAvailability::Healthy) {
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
  Network::TransportSocketOptionsSharedPtr upstreamTransportSocketOptions() const override {
    return context_->upstreamTransportSocketOptions();
  }

private:
  Upstream::HealthyAndDegradedLoad priority_load_;
  std::unique_ptr<Upstream::LoadBalancerContext> owned_context_;
  Upstream::LoadBalancerContext* context_{nullptr};
  const Upstream::LoadBalancerBase::HostAvailability host_availability_;
  const uint32_t host_priority_;
};

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
