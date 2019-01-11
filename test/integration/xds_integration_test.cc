#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// This is a minimal litmus test for the v2 xDS APIs.
class XdsIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  XdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam(), realTime()) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void createEnvoy() override {
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createApiTestServer(
        {
            "test/config/integration/server_xds.bootstrap.yaml",
            "test/config/integration/server_xds.cds.yaml",
            "test/config/integration/server_xds.eds.yaml",
            "test/config/integration/server_xds.lds.yaml",
            "test/config/integration/server_xds.rds.yaml",
        },
        {"http"});
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, XdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(XdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

} // namespace
} // namespace Envoy
