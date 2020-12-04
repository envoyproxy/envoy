#pragma once

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/upstream/upstream.h"

#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

class MockTransportSocketMatcher : public TransportSocketMatcher {
public:
  MockTransportSocketMatcher();
  MockTransportSocketMatcher(Network::TransportSocketFactoryPtr default_factory);
  ~MockTransportSocketMatcher() override;
  MOCK_METHOD(TransportSocketMatcher::MatchData, resolve,
              (const envoy::config::core::v3::Metadata*), (const));

private:
  Network::TransportSocketFactoryPtr socket_factory_;
  Stats::MockIsolatedStatsStore stats_store_;
  TransportSocketMatchStats stats_;
};

} // namespace Upstream
} // namespace Envoy
