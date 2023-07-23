#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class HttpConnPoolIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Set pool limit so that the test can use it's stats to validate that
      // the pool is deleted.
      envoy::config::cluster::v3::CircuitBreakers circuit_breakers;
      auto* threshold = circuit_breakers.mutable_thresholds()->Add();
      threshold->mutable_max_connection_pools()->set_value(1);
      auto* static_resources = bootstrap.mutable_static_resources();
      for (int i = 0; i < static_resources->clusters_size(); ++i) {
        static_resources->mutable_clusters(i)->mutable_circuit_breakers()->MergeFrom(
            circuit_breakers);
      }
    });
    HttpProtocolIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, HttpConnPoolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Tests that conn pools are cleaned up after becoming idle due to a LocalClose
TEST_P(HttpConnPoolIntegrationTest, PoolCleanupAfterLocalClose) {
  if (upstreamProtocol() == Http::CodecType::HTTP3 ||
      downstreamProtocol() == Http::CodecType::HTTP3) {
    // TODO(#26236) - Fix test flakiness over HTTP/3.
    return;
  }
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

// Verify that the drainConnections() cluster manager API works correctly.
TEST_P(HttpConnPoolIntegrationTest, PoolDrainAfterDrainApiSpecificCluster) {
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

  // Drain connection pools via API. Need to post this to the server thread.
  test_server_->server().dispatcher().post(
      [this] { test_server_->server().clusterManager().drainConnections("cluster_0", nullptr); });

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Validate that the pool is deleted when it becomes idle.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 0);
}

// Verify the drainConnections() with a predicate is able to filter host drains.
TEST_P(HttpConnPoolIntegrationTest, DrainConnectionsWithPredicate) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  // Perform a drain request which doesn't actually do a drain.
  test_server_->server().dispatcher().post([this] {
    test_server_->server().clusterManager().drainConnections(
        "cluster_0", [](const Upstream::Host&) { return false; });
  });

  // The existing upstream connection should continue to work.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  // Now do a drain that matches.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 1);
  test_server_->server().dispatcher().post([this] {
    test_server_->server().clusterManager().drainConnections(
        "cluster_0", [](const Upstream::Host&) { return true; });
  });
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 0);
}

// Verify that the drainConnections() cluster manager API works correctly.
TEST_P(HttpConnPoolIntegrationTest, PoolDrainAfterDrainApiAllClusters) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()->mutable_clusters()->Add()->MergeFrom(
        *bootstrap.mutable_static_resources()->mutable_clusters(0));
    bootstrap.mutable_static_resources()->mutable_clusters(1)->set_name("cluster_1");
  });

  setUpstreamCount(2);

  auto host = config_helper_.createVirtualHost("cluster_1.lyft.com", "/", "cluster_1");
  config_helper_.addVirtualHost(host);

  config_helper_.setDefaultHostAndRoute("cluster_0.lyft.com", "/");

  initialize();

  // Request Flow to cluster_0.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("cluster_0.lyft.com");
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  // Validate that the circuit breaker config is setup as we expect.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 1);

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  auto first_connection = std::move(fake_upstream_connection_);
  codec_client_->close();

  // Request Flow to cluster_1.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("cluster_1.lyft.com");
  response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest(1);

  // Validate that the circuit breaker config is setup as we expect.
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.cx_pool_open", 1);

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  // Drain connection pools via API. Need to post this to the server thread.
  test_server_->server().dispatcher().post(
      [this] { test_server_->server().clusterManager().drainConnections(nullptr); });

  ASSERT_TRUE(first_connection->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.cx_pool_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.cx_pool_open", 0);
}

} // namespace
} // namespace Envoy
