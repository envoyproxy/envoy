#pragma once

#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Upstream {

class LoadBalancerContextBase : public LoadBalancerContext {
public:
  absl::optional<uint64_t> computeHashKey() override { return {}; }

  const Network::Connection* downstreamConnection() const override { return nullptr; }

  const Router::MetadataMatchCriteria* metadataMatchCriteria() override { return nullptr; }

  const StreamInfo::StreamInfo* requestStreamInfo() const override { return nullptr; }

  const Http::RequestHeaderMap* downstreamHeaders() const override { return nullptr; }

  const HealthyAndDegradedLoad&
  determinePriorityLoad(const PrioritySet&, const HealthyAndDegradedLoad& original_priority_load,
                        const Upstream::RetryPriority::PriorityMappingFunc&) override {
    return original_priority_load;
  }

  bool shouldSelectAnotherHost(const Host&) override { return false; }

  uint32_t hostSelectionRetryCount() const override { return 1; }

  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override { return nullptr; }

  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return nullptr;
  }

  absl::optional<OverrideHost> overrideHostToSelect() const override { return {}; }

  void setOrcaLoadReportCallbacks(std::weak_ptr<OrcaLoadReportCallbacks>) override {}
};

} // namespace Upstream
} // namespace Envoy
