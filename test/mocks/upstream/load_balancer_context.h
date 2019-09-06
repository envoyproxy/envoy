#include "envoy/upstream/load_balancer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

class MockLoadBalancerContext : public LoadBalancerContext {
public:
  MockLoadBalancerContext();
  ~MockLoadBalancerContext() override;

  MOCK_METHOD0(computeHashKey, absl::optional<uint64_t>());
  MOCK_METHOD0(metadataMatchCriteria, Router::MetadataMatchCriteria*());
  MOCK_CONST_METHOD0(downstreamConnection, const Network::Connection*());
  MOCK_CONST_METHOD0(downstreamHeaders, const Http::HeaderMap*());
  MOCK_METHOD2(determinePriorityLoad,
               const HealthyAndDegradedLoad&(const PrioritySet&, const HealthyAndDegradedLoad&));
  MOCK_METHOD1(shouldSelectAnotherHost, bool(const Host&));
  MOCK_CONST_METHOD0(hostSelectionRetryCount, uint32_t());
  MOCK_CONST_METHOD0(upstreamSocketOptions, Network::Socket::OptionsSharedPtr());

private:
  HealthyAndDegradedLoad priority_load_;
};

} // namespace Upstream
} // namespace Envoy
