#pragma once

#include <string>

#include "envoy/upstream/upstream.h"

#include "common/stats/isolated_store_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {
namespace Outlier {

class MockTransportSocketMatcher : public TransportSocketMatcher {
public:
  MockTransportSocketMatcher();
  ~MockTransportSocketMatcher() override;
  MOCK_CONST_METHOD1(resolve,
                     TransportSocketMatcher::MatchData(const envoy::api::v2::core::Metadata&));

private:
  Network::TransportSocketFactoryPtr socket_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  TransportSocketMatchStats stats_;
};

} // namespace Outlier
} // namespace Upstream
} // namespace Envoy