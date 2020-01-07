#pragma once

#include <string>

#include "envoy/config/core/v3alpha/base.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/stats/isolated_store_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

class MockTransportSocketMatcher : public TransportSocketMatcher {
public:
  MockTransportSocketMatcher();
  MockTransportSocketMatcher(Network::TransportSocketFactoryPtr default_factory);
  ~MockTransportSocketMatcher() override;
  MOCK_CONST_METHOD1(
      resolve, TransportSocketMatcher::MatchData(const envoy::config::core::v3alpha::Metadata&));

private:
  Network::TransportSocketFactoryPtr socket_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  TransportSocketMatchStats stats_;
};

} // namespace Upstream
} // namespace Envoy
