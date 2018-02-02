#include "uds_integration_test.h"

#include "common/event/dispatcher_impl.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, UdsIntegrationTest,
                        testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                                         testing::Bool()));

TEST_P(UdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
#if !defined(__linux__)
  if (abstract_namespace_) {
    EXPECT_THROW(testRouterRequestAndResponseWithBody(1024, 512, false), EnvoyException);
    return;
  }
#endif
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(UdsIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
#if !defined(__linux__)
  if (abstract_namespace_) {
    EXPECT_THROW(testRouterHeaderOnlyRequestAndResponse(true), EnvoyException);
    return;
  }
#endif
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(UdsIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
#if !defined(__linux__)
  if (abstract_namespace_) {
    EXPECT_THROW(testRouterUpstreamDisconnectBeforeResponseComplete(), EnvoyException);
    return;
  }
#endif
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(UdsIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
#if !defined(__linux__)
  if (abstract_namespace_) {
    EXPECT_THROW(testRouterDownstreamDisconnectBeforeRequestComplete(), EnvoyException);
    return;
  }
#endif
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(UdsIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
#if !defined(__linux__)
  if (abstract_namespace_) {
    EXPECT_THROW(testRouterDownstreamDisconnectBeforeResponseComplete(), EnvoyException);
    return;
  }
#endif
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

} // namespace Envoy
