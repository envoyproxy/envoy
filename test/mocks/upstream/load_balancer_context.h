#pragma once
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/load_balancer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

class MockLoadBalancerContext : public LoadBalancerContext {
public:
  MockLoadBalancerContext();
  ~MockLoadBalancerContext() override;

  MOCK_METHOD(absl::optional<uint64_t>, computeHashKey, ());
  MOCK_METHOD(Router::MetadataMatchCriteria*, metadataMatchCriteria, ());
  MOCK_METHOD(const Network::Connection*, downstreamConnection, (), (const));
  MOCK_METHOD(const StreamInfo::StreamInfo*, requestStreamInfo, (), (const));
  MOCK_METHOD(const Http::RequestHeaderMap*, downstreamHeaders, (), (const));
  MOCK_METHOD(const HealthyAndDegradedLoad&, determinePriorityLoad,
              (const PrioritySet&, const HealthyAndDegradedLoad&,
               const Upstream::RetryPriority::PriorityMappingFunc&));
  MOCK_METHOD(bool, shouldSelectAnotherHost, (const Host&));
  MOCK_METHOD(uint32_t, hostSelectionRetryCount, (), (const));
  MOCK_METHOD(Network::Socket::OptionsSharedPtr, upstreamSocketOptions, (), (const));
  MOCK_METHOD(Network::TransportSocketOptionsConstSharedPtr, upstreamTransportSocketOptions, (),
              (const));
  MOCK_METHOD(absl::optional<OverrideHost>, overrideHostToSelect, (), (const));
  MOCK_METHOD(void, setOrcaLoadReportCallbacks, (std::weak_ptr<OrcaLoadReportCallbacks>));

private:
  HealthyAndDegradedLoad priority_load_;
};

} // namespace Upstream
} // namespace Envoy
