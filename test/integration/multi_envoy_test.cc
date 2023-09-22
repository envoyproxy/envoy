#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class MultiEnvoyTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addRuntimeOverride("envoy.disallow_global_stats", "true");
    HttpProtocolIntegrationTest::initialize();
  }

  ~MultiEnvoyTest() override { test_server_.reset(); }

  // Create an envoy in front of the original Envoy.
  void createL1Envoy();

  IntegrationTestServerPtr l1_server_;
};

void MultiEnvoyTest::createL1Envoy() {
  std::vector<uint32_t> ports;
  std::vector<uint32_t> zero;
  int l2_port = lookupPort("http");
  for (auto& upstream : fake_upstreams_) {
    if (upstream->localAddress()->ip()) {
      ports.push_back(l2_port);
      zero.push_back(0);
    }
  }
  ConfigHelper l1_helper(version_, config_helper_.bootstrap());
  l1_helper.setPorts(zero, true); // Zero out ports set by config_helper_'s finalize();
  const std::string bootstrap_path = finalizeConfigWithPorts(l1_helper, ports, use_lds_);

  std::vector<std::string> named_ports;
  const auto& static_resources = config_helper_.bootstrap().static_resources();
  named_ports.reserve(static_resources.listeners_size());
  for (int i = 0; i < static_resources.listeners_size(); ++i) {
    named_ports.push_back(static_resources.listeners(i).name() + "_l1");
  }
  createGeneratedApiTestServer(bootstrap_path, named_ports, {false, true, false}, false,
                               l1_server_);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiEnvoyTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// A simple test to make sure that traffic can flow between client - L1 - L2 - upstream.
// This test does not currently support mixed protocol hops, or much of the other envoy test
// framework knobs.
TEST_P(MultiEnvoyTest, SimpleRequestAndResponse) {
  initialize();
  createL1Envoy();

  codec_client_ = makeHttpConnection(lookupPort("http_l1"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ(1, test_server_->counter("http.config_test.rq_total"));
  EXPECT_EQ(1, l1_server_->counter("http.config_test.rq_total"));
  EXPECT_EQ("200", response->headers().getStatusValue());

  l1_server_.reset();

  // At the point that the l1 is shut down, requests to the L2 should work.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Similar to SimpleRequestAndResponse but tear down the L2 first.
TEST_P(MultiEnvoyTest, SimpleRequestAndResponseL2Teardown) {
  initialize();
  createL1Envoy();

  codec_client_ = makeHttpConnection(lookupPort("http_l1"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ(1, test_server_->counter("http.config_test.rq_total"));
  EXPECT_EQ(1, l1_server_->counter("http.config_test.rq_total"));
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_.reset();

  // At the point that the l2 is shut down, requests to the L2 should 503.
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());

  l1_server_.reset();
}

} // namespace
} // namespace Envoy
