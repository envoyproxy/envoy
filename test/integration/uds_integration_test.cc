#include "uds_integration_test.h"

#include "common/event/dispatcher_impl.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(TestParameters, UdsIntegrationTest,
                        testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
#if defined(__linux__)
                                         testing::Values(false, true)
#else
                                         testing::Values(false)
#endif
                                             ));

TEST_P(UdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(UdsIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(UdsIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(UdsIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(UdsIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

} // namespace Envoy
