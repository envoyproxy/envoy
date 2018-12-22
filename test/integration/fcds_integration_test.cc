#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration test for EDS features. EDS is consumed via filesystem
// subscription.
class FcdsIntegrationTest : public HttpIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  FcdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  void initialize() override {
    // setUpstreamCount(4);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* listener_0 = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener_0->mutable_filter_chains()->Clear();
      listener_0->mutable_fcds_config()->mutable_config()->set_path("/");
    });
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, FcdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(FcdsIntegrationTest, test) { initialize(); }

} // namespace
} // namespace Envoy
