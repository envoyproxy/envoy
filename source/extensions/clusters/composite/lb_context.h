#pragma once

#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

/**
 * Load balancer context wrapper for composite cluster.
 *
 * This wrapper provides a pass-through for most LoadBalancerContext methods while adding
 * the ability to track the selected cluster index for routing decisions.
 */
class CompositeLoadBalancerContext : public Upstream::LoadBalancerContext {
public:
  CompositeLoadBalancerContext(Upstream::LoadBalancerContext* base_context,
                               size_t selected_cluster_index)
      : base_context_(base_context), selected_cluster_index_(selected_cluster_index) {}

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

  // Composite-specific method
  size_t selectedClusterIndex() const { return selected_cluster_index_; }

private:
  Upstream::LoadBalancerContext* base_context_;
  size_t selected_cluster_index_;
};

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
