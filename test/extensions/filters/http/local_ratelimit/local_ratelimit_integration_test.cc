#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class LocalRateLimitFilterIntegrationTest : public Event::TestUsingSimulatedTime,
                                            public HttpProtocolIntegrationTest {
protected:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }

  const std::string filter_config_ =
      R"EOF(
name: envoy.filters.http.local_ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
  stat_prefix: http_local_rate_limiter
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 1000s
  filter_enabled:
    runtime_key: local_rate_limit_enabled
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enforced:
    runtime_key: local_rate_limit_enforced
    default_value:
      numerator: 100
      denominator: HUNDRED
  response_headers_to_add:
    - append: false
      header:
        key: x-local-rate-limit
        value: 'true'
  local_rate_limit_per_downstream_connection: {}
)EOF";
};

INSTANTIATE_TEST_SUITE_P(Protocols, LocalRateLimitFilterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(LocalRateLimitFilterIntegrationTest, DenyRequestPerProcess) {
  initializeFilter(fmt::format(filter_config_, "false"));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  cleanupUpstreamAndDownstream();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeRequestWithBody(default_request_headers_, 0);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("429", response->headers().getStatusValue());
  EXPECT_EQ(18, response->body().size());
}

TEST_P(LocalRateLimitFilterIntegrationTest, DenyRequestWithinSameConnection) {
  initializeFilter(fmt::format(filter_config_, "true"));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("429", response->headers().getStatusValue());
  EXPECT_EQ(18, response->body().size());
}

TEST_P(LocalRateLimitFilterIntegrationTest, PermitRequestAcrossDifferentConnections) {
  initializeFilter(fmt::format(filter_config_, "true"));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  cleanupUpstreamAndDownstream();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());
}

} // namespace
} // namespace Envoy
