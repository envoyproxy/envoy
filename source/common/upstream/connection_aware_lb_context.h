#pragma once

#include <cstdint>
#include <functional>

#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/upstream/load_balancer_context_base.h"

namespace Envoy {
namespace Upstream {

// Wraps the caller's load balancer context to reject and prime hosts without a
// ready connection, bounding re-picks by `retry_budget`.
class ConnectionAwareLbContext : public LoadBalancerContextBase {
public:
  ConnectionAwareLbContext(LoadBalancerContext* wrapped,
                           std::function<bool(const Host&)> needs_priming,
                           std::function<void(const Host&)> prime, uint32_t retry_budget)
      : wrapped_(wrapped), needs_priming_(std::move(needs_priming)), prime_(std::move(prime)),
        retry_budget_(retry_budget) {}

  // Reject (and prime) hosts without a ready connection so the LB re-picks.
  bool shouldSelectAnotherHost(const Host& host) override {
    if (wrapped_ != nullptr && wrapped_->shouldSelectAnotherHost(host)) {
      return true;
    }
    if (needs_priming_(host)) {
      prime_(host); // self-heal
      return true;
    }
    return false;
  }

  uint32_t hostSelectionRetryCount() const override {
    const uint32_t wrapped_count = wrapped_ != nullptr ? wrapped_->hostSelectionRetryCount() : 0;
    return std::max(wrapped_count, retry_budget_);
  }

  // Forward the remaining context surface to the wrapped context when present.
  std::optional<uint64_t> computeHashKey() override {
    return wrapped_ != nullptr ? wrapped_->computeHashKey() : std::nullopt;
  }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    return wrapped_ != nullptr ? wrapped_->metadataMatchCriteria() : nullptr;
  }
  const Network::Connection* downstreamConnection() const override {
    return wrapped_ != nullptr ? wrapped_->downstreamConnection() : nullptr;
  }
  StreamInfo::StreamInfo* requestStreamInfo() const override {
    return wrapped_ != nullptr ? wrapped_->requestStreamInfo() : nullptr;
  }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return wrapped_ != nullptr ? wrapped_->downstreamHeaders() : nullptr;
  }
  const HealthyAndDegradedLoad&
  determinePriorityLoad(const PrioritySet& priority_set,
                        const HealthyAndDegradedLoad& original_priority_load,
                        const Upstream::RetryPriority::PriorityMappingFunc& mapping) override {
    return wrapped_ != nullptr
               ? wrapped_->determinePriorityLoad(priority_set, original_priority_load, mapping)
               : original_priority_load;
  }
  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return wrapped_ != nullptr ? wrapped_->upstreamSocketOptions() : nullptr;
  }
  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return wrapped_ != nullptr ? wrapped_->upstreamTransportSocketOptions() : nullptr;
  }
  OptRef<const OverrideHost> overrideHostToSelect() const override {
    return wrapped_ != nullptr ? wrapped_->overrideHostToSelect() : OptRef<const OverrideHost>{};
  }
  void onAsyncHostSelection(HostConstSharedPtr&& host, std::string&& details) override {
    if (wrapped_ != nullptr) {
      wrapped_->onAsyncHostSelection(std::move(host), std::move(details));
    }
  }
  void setHeadersModifier(std::function<void(Http::ResponseHeaderMap&)> modifier) override {
    if (wrapped_ != nullptr) {
      wrapped_->setHeadersModifier(std::move(modifier));
    }
  }

private:
  LoadBalancerContext* const wrapped_;
  const std::function<bool(const Host&)> needs_priming_;
  const std::function<void(const Host&)> prime_;
  const uint32_t retry_budget_;
};

} // namespace Upstream
} // namespace Envoy
