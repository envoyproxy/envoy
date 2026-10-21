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

  MOCK_METHOD(std::optional<uint64_t>, computeHashKey, (), (override));
  MOCK_METHOD(Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (override));
  MOCK_METHOD(const Network::Connection*, downstreamConnection, (), (const, override));
  MOCK_METHOD(StreamInfo::StreamInfo*, requestStreamInfo, (), (const, override));
  MOCK_METHOD(const Http::RequestHeaderMap*, downstreamHeaders, (), (const, override));
  MOCK_METHOD(const HealthyAndDegradedLoad&, determinePriorityLoad,
              (const PrioritySet&, const HealthyAndDegradedLoad&,
               const Upstream::RetryPriority::PriorityMappingFunc&),
              (override));
  MOCK_METHOD(bool, shouldSelectAnotherHost, (const Host&), (override));
  MOCK_METHOD(uint32_t, hostSelectionRetryCount, (), (const, override));
  MOCK_METHOD(Network::Socket::OptionsSharedPtr, upstreamSocketOptions, (), (const, override));
  MOCK_METHOD(Network::TransportSocketOptionsConstSharedPtr, upstreamTransportSocketOptions, (),
              (const, override));
  MOCK_METHOD(OptRef<const OverrideHost>, overrideHostToSelect, (), (const, override));
  MOCK_METHOD(void, onAsyncHostSelection, (HostConstSharedPtr && host, std::string&& details),
              (override));
  MOCK_METHOD(void, setHeadersModifier, (std::function<void(Http::ResponseHeaderMap&)>),
              (override));

private:
  HealthyAndDegradedLoad priority_load_;
};

} // namespace Upstream
} // namespace Envoy
