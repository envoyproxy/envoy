#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SamplingDecision {
namespace {

class SamplingDecisionIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  SamplingDecisionIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SamplingDecisionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test that the filter stores sampling metadata correctly.
TEST_P(SamplingDecisionIntegrationTest, BasicSamplingMetadata) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.sampling_decision
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
  runtime_key: test.sampling.key
  percent_sampled:
    numerator: 100
    denominator: HUNDRED
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}},
      1024);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test with custom metadata namespace.
TEST_P(SamplingDecisionIntegrationTest, CustomMetadataNamespace) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.sampling_decision
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
  runtime_key: test.sampling.key
  percent_sampled:
    numerator: 50
    denominator: HUNDRED
  metadata_namespace: custom.sampling.namespace
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}},
      1024);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that the filter works with independent randomness.
TEST_P(SamplingDecisionIntegrationTest, IndependentRandomness) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.sampling_decision
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
  runtime_key: test.sampling.key
  percent_sampled:
    numerator: 100
    denominator: HUNDRED
  use_independent_randomness: true
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}},
      1024);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that multiple sampling decision filters can coexist with different namespaces.
TEST_P(SamplingDecisionIntegrationTest, MultipleFiltersWithDifferentNamespaces) {
  const std::string filter_config1 = R"EOF(
name: envoy.filters.http.sampling_decision
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
  runtime_key: test.sampling.key1
  percent_sampled:
    numerator: 100
    denominator: HUNDRED
  metadata_namespace: sampling.decision1
)EOF";

  const std::string filter_config2 = R"EOF(
name: envoy.filters.http.sampling_decision
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
  runtime_key: test.sampling.key2
  percent_sampled:
    numerator: 50
    denominator: HUNDRED
  metadata_namespace: sampling.decision2
)EOF";

  config_helper_.prependFilter(filter_config1);
  config_helper_.prependFilter(filter_config2);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}},
      1024);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that the filter continues processing regardless of sampling decision.
TEST_P(SamplingDecisionIntegrationTest, FilterAlwaysContinues) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.sampling_decision
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
  runtime_key: test.sampling.key
  percent_sampled:
    numerator: 0
    denominator: HUNDRED
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Even with 0% sampling, the request should still be processed.
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}},
      1024);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace SamplingDecision
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
