#include "envoy/extensions/filters/http/connection_pool_cardinality/v3/connection_pool_cardinality.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class ConnectionPoolCardinalityIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void setupFilter(uint32_t connection_pool_count) {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter http_filter;
    http_filter.set_name("envoy.filters.http.connection_pool_cardinality");
    envoy::extensions::filters::http::connection_pool_cardinality::v3::
        ConnectionPoolCardinalityConfig config;
    config.set_connection_pool_count(connection_pool_count);
    http_filter.mutable_typed_config()->PackFrom(config);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(http_filter));
  }
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ConnectionPoolCardinalityIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(ConnectionPoolCardinalityIntegrationTest, FilterProcessesRequestsWithHighCardinality) {
  // Set connection pool count to 100 to enable multiple connection pools
  autonomous_upstream_ = true;
  setupFilter(100);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send multiple requests.
  // Odds are the different requests will go to different connection pools with high probability.
  // (1/100)^10 we only connect to the same connection pool.
  for (int i = 0; i < 10; ++i) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  // We should have more than 1 connection due to connection pool cardinality.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 10);
  test_server_->waitForGaugeGe("cluster.cluster_0.upstream_cx_active", 1);
}

TEST_P(ConnectionPoolCardinalityIntegrationTest, SingleConnectionWithLowCardinality) {
  autonomous_upstream_ = true;
  setupFilter(1);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Multiple request should just only create one connection.
  for (int i = 0; i < 10; ++i) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  // We should only have a single connection given one connection pool.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 10);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 1);
}

} // namespace
} // namespace Envoy
