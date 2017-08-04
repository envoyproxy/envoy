#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// This is a minimal litmus test for the v2 xDS APIs. TODO(htuch): Convert all integration tests to
// be parameterized with v2 configs.
class XdsIntegrationTest : public BaseIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  XdsIntegrationTest() : BaseIntegrationTest(GetParam()) {}

  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createApiTestServer("test/config/integration/server_xds.json",
                        {
                            .bootstrap_path_ = "test/config/integration/server_xds.bootstrap.json",
                            .cds_path_ = "test/config/integration/server_xds.cds.json",
                            .eds_path_ = "test/config/integration/server_xds.eds.json",
                        },
                        {"http"});
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, XdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(XdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, false);
}

} // namespace
} // namespace Envoy
