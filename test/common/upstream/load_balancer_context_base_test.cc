#include "source/common/upstream/load_balancer_context_base.h"

#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(LoadBalancerContextBaseTest, LoadBalancerContextBaseTest) {
  {
    LoadBalancerContextBase context;
    MockPrioritySet mock_priority_set;
    HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({100, 0, 0}),
                                         Upstream::DegradedLoad({0, 0, 0})};
    RetryPriority::PriorityMappingFunc empty_func =
        [](const Upstream::HostDescription&) -> absl::optional<uint32_t> { return absl::nullopt; };
    MockHost mock_host;

    EXPECT_EQ(absl::nullopt, context.computeHashKey());
    EXPECT_EQ(nullptr, context.downstreamConnection());
    EXPECT_EQ(nullptr, context.metadataMatchCriteria());
    EXPECT_EQ(nullptr, context.downstreamHeaders());

    EXPECT_EQ(&priority_load,
              &(context.determinePriorityLoad(mock_priority_set, priority_load, empty_func)));
    EXPECT_EQ(false, context.shouldSelectAnotherHost(mock_host));
    EXPECT_EQ(1, context.hostSelectionRetryCount());
    EXPECT_EQ(nullptr, context.upstreamSocketOptions());
    EXPECT_EQ(nullptr, context.upstreamTransportSocketOptions());
    EXPECT_EQ(absl::nullopt, context.overrideHostToSelect());
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
