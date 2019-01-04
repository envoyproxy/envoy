#include "test/integration/integration.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

// This test loops through all of the json config files submitted as of
// 11/10/2017 and verifies Envoy continues to load the json version of their
// configuration.
class LegacyJsonIntegrationTest : public BaseIntegrationTest,
                                  public testing::TestWithParam<Network::Address::IpVersion> {
public:
  LegacyJsonIntegrationTest() : BaseIntegrationTest(GetParam(), realTime()) {}

  void SetUp() override {
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
    registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
    registerPort("cluster_with_buffer_limits",
                 fake_upstreams_.back()->localAddress()->ip()->port());
  }
};

TEST_P(LegacyJsonIntegrationTest, TestServer) {
  createTestServer("test/config/integration/server.json", {"http"});
}

TEST_P(LegacyJsonIntegrationTest, TestServerHttp2) {
  createTestServer("test/config/integration/server_http2.json", {"echo"});
}

TEST_P(LegacyJsonIntegrationTest, TestServerHttp2Upstream) {
  createTestServer("test/config/integration/server_http2_upstream.json", {"http"});
}

TEST_P(LegacyJsonIntegrationTest, TestServerProxyProto) {
  createTestServer("test/config/integration/server_proxy_proto.json", {"http"});
}

TEST_P(LegacyJsonIntegrationTest, TestServerSsl) {
  createTestServer("test/config/integration/server_ssl.json", {"http"});
}

TEST_P(LegacyJsonIntegrationTest, TestTcpProxy) {
  createTestServer("test/config/integration/tcp_proxy.json", {"tcp_proxy"});
}

TEST_P(LegacyJsonIntegrationTest, TestServerUds) {
  createTestServer("test/config/integration/server_uds.json", {"http"});
}

TEST_P(LegacyJsonIntegrationTest, TestServerXfc) {
  TestEnvironment::ParamMap param_map;
  param_map["forward_client_cert"] = "forward_only";
  param_map["set_current_client_cert_details"] = "";
  std::string config = TestEnvironment::temporaryFileSubstitute(
      "test/config/integration/server_xfcc.json", param_map, port_map_, version_);
  IntegrationTestServer::create(config, version_, nullptr, false, timeSystem(), *api_);
}

TEST_P(LegacyJsonIntegrationTest, TestEchoServer) {
  createTestServer("test/config/integration/echo_server.json", {"echo"});
}

TEST_P(LegacyJsonIntegrationTest, TestServerGrpcJsonTranscoder) {
  createTestServer("test/config/integration/server_grpc_json_transcoder.json", {"http"});
}

INSTANTIATE_TEST_CASE_P(IpVersions, LegacyJsonIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);
} // namespace Envoy
