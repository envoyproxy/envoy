#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class HttpConnPoolIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.conn_pool_delete_when_idle",
                                      "true");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Set pool limit so that the test can use it's stats to validate that
      // the pool is deleted.
      envoy::config::cluster::v3::CircuitBreakers circuit_breakers;
      auto* threshold = circuit_breakers.mutable_thresholds()->Add();
      threshold->mutable_max_connection_pools()->set_value(1);
      bootstrap.mutable_static_resources()
          ->mutable_clusters(0)
          ->mutable_circuit_breakers()
          ->MergeFrom(circuit_breakers);
    });
    HttpProtocolIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, HttpConnPoolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Tests that conn pools are cleaned up after becoming idle due to a LocalClose
TEST_P(HttpConnPoolIntegrationTest, PoolCleanupAfterLocalClose) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Make Envoy close the upstream connection after a single request.
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_common_http_protocol_options()
        ->mutable_max_requests_per_connection()
        ->set_value(1);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  // Validate that the circuit breaker config is setup as we expect.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 1);

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Validate that the pool is deleted when it becomes idle.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 0);
}

// Tests that conn pools are cleaned up after becoming idle due to a RemoteClose
TEST_P(HttpConnPoolIntegrationTest, PoolCleanupAfterRemoteClose) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  // Validate that the circuit breaker config is setup as we expect.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 1);

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  ASSERT_TRUE(fake_upstream_connection_->close());

  // Validate that the pool is deleted when it becomes idle.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 0);
}

} // namespace
} // namespace Envoy
