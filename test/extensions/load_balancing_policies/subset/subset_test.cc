#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.h"

#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/extensions/load_balancing_policies/subset/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Subset {
namespace {

// Test to improve coverage of the SubsetLoadBalancerFactory.
TEST(LoadBalancerContextWrapperTest, LoadBalancingContextWrapperTest) {
  testing::NiceMock<Upstream::MockLoadBalancerContext> mock_context;

  ProtobufWkt::Struct empty_struct;
  Router::MetadataMatchCriteriaImpl match_criteria(empty_struct);
  ON_CALL(mock_context, metadataMatchCriteria()).WillByDefault(testing::Return(&match_criteria));

  Upstream::SubsetLoadBalancer::LoadBalancerContextWrapper wrapper(&mock_context,
                                                                   std::set<std::string>{});

  EXPECT_CALL(mock_context, computeHashKey());
  wrapper.computeHashKey();

  EXPECT_CALL(mock_context, downstreamConnection());
  wrapper.downstreamConnection();

  EXPECT_CALL(mock_context, downstreamHeaders());
  wrapper.downstreamHeaders();

  EXPECT_CALL(mock_context, hostSelectionRetryCount());

  wrapper.hostSelectionRetryCount();

  EXPECT_CALL(mock_context, upstreamSocketOptions());
  wrapper.upstreamSocketOptions();

  EXPECT_CALL(mock_context, upstreamTransportSocketOptions());
  wrapper.upstreamTransportSocketOptions();

  EXPECT_CALL(mock_context, overrideHostToSelect());
  wrapper.overrideHostToSelect();
}

} // namespace
} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
