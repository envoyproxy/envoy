#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using testing::HasSubstr;

// This is a minimal litmus test for the v2 xDS APIs.
class XdsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  XdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {
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

INSTANTIATE_TEST_SUITE_P(IpVersions, XdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

typedef HttpProtocolIntegrationTest LdsIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, LdsIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1}, {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Sample test making sure our config framework correctly reloads listeners.
TEST_P(LdsIntegrationTest, ReloadConfig) {
  use_lds_ = true;
  autonomous_upstream_ = true;
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  const std::string lds_filename =
      config_helper_.bootstrap().dynamic_resources().lds_config().path();

  // HTTP 1.0 is disabled by default.
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);

  // Turn up HTTP/1.0 proxying.
  config_helper_.allowFurtherEdits();
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.mutable_http_protocol_options()->set_accept_http_10(true);
        hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
      });
  config_helper_.applyConfigModifiers();

  // Create an LDS response with the new config, and reload config.
  envoy::api::v2::DiscoveryResponse lds = config_helper_.createLdsResponse("1");
  std::string file = TestEnvironment::writeStringToFileForTest(
      "new_lds_file", MessageUtil::getJsonStringFromMessage(lds));
  TestUtility::renameFile(file, lds_filename);

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);

  // HTTP 1.0 should now be enabled.
  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response2, false);
  EXPECT_THAT(response2, HasSubstr("HTTP/1.0 200 OK\r\n"));
}

} // namespace
} // namespace Envoy
