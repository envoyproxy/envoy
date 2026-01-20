#pragma once

#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

// CompositeLoadBalancerContext wraps the original load balancer context to provide
// retry-aware cluster selection. It delegates most operations to the underlying
// context while tracking the selected cluster index for debugging purposes.
class CompositeLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  CompositeLoadBalancerContext(Upstream::LoadBalancerContext* context,
                               size_t selected_cluster_index)
      : selected_cluster_index_(selected_cluster_index) {
    if (context == nullptr) {
      owned_context_ = std::make_unique<Upstream::LoadBalancerContextBase>();
      context_ = owned_context_.get();
    } else {
      context_ = context;
    }
  }

  // Upstream::LoadBalancerContext - delegate all methods to the wrapped context.
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

  StreamInfo::StreamInfo* requestStreamInfo() const override {
    return context_->requestStreamInfo();
  }

  const Upstream::HealthyAndDegradedLoad& determinePriorityLoad(
      const Upstream::PrioritySet& priority_set,
      const Upstream::HealthyAndDegradedLoad& original_priority_load,
      const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) override {
    // Delegate priority load determination to the wrapped context.
    // The selected cluster maintains its own priority handling.
    return context_->determinePriorityLoad(priority_set, original_priority_load,
                                           priority_mapping_func);
  }

  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return context_->shouldSelectAnotherHost(host);
  }

  uint32_t hostSelectionRetryCount() const override { return context_->hostSelectionRetryCount(); }

  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return context_->upstreamSocketOptions();
  }

  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return context_->upstreamTransportSocketOptions();
  }

  absl::optional<OverrideHost> overrideHostToSelect() const override {
    return context_->overrideHostToSelect();
  }

  void onAsyncHostSelection(Upstream::HostConstSharedPtr&& host, std::string&& details) override {
    context_->onAsyncHostSelection(std::move(host), std::move(details));
  }

  void setHeadersModifier(std::function<void(Http::ResponseHeaderMap&)> modifier) override {
    context_->setHeadersModifier(std::move(modifier));
  }

  // Get the selected cluster index for this request.
  size_t selectedClusterIndex() const { return selected_cluster_index_; }

private:
  std::unique_ptr<Upstream::LoadBalancerContext> owned_context_;
  Upstream::LoadBalancerContext* context_{nullptr};
  const size_t selected_cluster_index_;
};

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
