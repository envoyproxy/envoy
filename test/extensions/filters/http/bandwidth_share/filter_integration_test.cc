#include <chrono>
#include <string>

#include "envoy/extensions/filters/http/bandwidth_share/v3/bandwidth_share.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_protocol_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace {

using ::testing::_;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Lt;
using ::testing::Not;
using ::testing::Property;
using ::testing::ResultOf;
using ::testing::StartsWith;

using BandwidthShareProto = envoy::extensions::filters::http::bandwidth_share::v3::BandwidthShare;
using HttpConnectionManagerProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

template <class NameMatcher> auto MetricNameMatches(NameMatcher name_matcher) {
  return Property(&Stats::Metric::name, name_matcher);
}

template <class MetricMatcher> auto HasMetric(MetricMatcher metric_matcher) {
  return Contains(
      ResultOf([](const auto& metric) -> const Stats::Metric& { return *metric; }, metric_matcher));
}

class BandwidthShareIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public HttpIntegrationTest,
                                      public testing::Test {
public:
  BandwidthShareIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void initializeFilter(const std::string& config) {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->mutable_per_connection_buffer_limit_bytes()->set_value(4096); // Set to 4KB
    });
    config_helper_.prependFilter(config);
    initialize();
  }

  void initializeFilterWithRouteConfig(const std::string& filter_config,
                                       const std::string& route_config) {
    config_helper_.addConfigModifier([route_config](HttpConnectionManagerProto& hcm) {
      BandwidthShareProto per_route_config;
      TestUtility::loadFromYaml(route_config, per_route_config);
      auto* typed_config = hcm.mutable_route_config()
                               ->mutable_virtual_hosts(0)
                               ->mutable_routes(0)
                               ->mutable_typed_per_filter_config();
      (*typed_config)["bwshare"].PackFrom(per_route_config);
    });
    initializeFilter(filter_config);
  }

  std::string disabledFilter() {
    // If neither request_bucket_id nor response_bucket_id is set the filter is disabled.
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
  )";
  }
  std::string twoWay1kFilter() {
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
      request_limit: {bucket_id: "bucket1", kbps: {default_value: 1, runtime_key: "a"}}
      response_limit: {bucket_id: "bucket2", kbps: {default_value: 1, runtime_key: "a"}}
  )";
  }
  std::string runtimeControlledRequestFilter() {
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
      request_limit:
        bucket_id: "runtime_bucket"
        kbps: {default_value: 1, runtime_key: "bandwidth_share.runtime_request_kbps"}
  )";
  }
  std::string responseOnly1kFilter() {
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
      response_limit: {bucket_id: "response_bucket", kbps: {default_value: 1, runtime_key: "response"}}
  )";
  }
  std::string responseTrailersFilter() {
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
      request_limit: {bucket_id: "trailer_request", kbps: {default_value: 1, runtime_key: "trailer_request"}}
      response_limit: {bucket_id: "trailer_response", kbps: {default_value: 1, runtime_key: "trailer_response"}}
      enable_response_trailers: true
      response_trailer_prefix: x-test-
  )";
  }
  std::string responseTrailersWithoutDelayFilter() {
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
      response_limit: {bucket_id: "trailer_response", kbps: {default_value: 1024}}
      enable_response_trailers: true
      response_trailer_prefix: x-test-
  )";
  }
  std::string tenantTaggedRouteConfig() {
    return R"(
    request_limit: {bucket_id: "route_bucket", kbps: {default_value: 1, runtime_key: "route_request"}}
    tenant_name_selector:
      on_no_match:
        action:
          name: use_constant_string_as_tenant_name
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: gold
    tenant_configs:
      "gold": {include_stats_tag: true}
  )";
  }
  uint64_t counterValue(const std::string& name) {
    const auto counter = test_server_->counter(name);
    return counter != nullptr ? counter->value() : 0;
  }

  uint64_t gaugeValue(const std::string& name) {
    const auto gauge = test_server_->gauge(name);
    return gauge != nullptr ? gauge->value() : 0;
  }

  absl::Status setRuntimeInt(const std::string& key, uint32_t value) {
    BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "POST", absl::StrCat("/runtime_modify?", key, "=", value), "",
        Http::CodecType::HTTP1, version_);
    if (!admin_response->complete()) {
      return absl::InternalError("runtime_modify admin request did not complete");
    }
    if (admin_response->headers().getStatusValue() != "200") {
      return absl::InternalError(absl::StrCat("runtime_modify admin request returned ",
                                              admin_response->headers().getStatusValue()));
    }
    return absl::OkStatus();
  }

  void makeDownstreamConnection() {
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  // Only for streams that are not expected to be bandwidth limited.
  void sendRequestAndResponse() {
    auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
    codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
    codec_client_->sendTrailers(request_encoder, any_request_trailers_);
    waitForNextUpstreamRequest();
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(response_headers_, false);
    upstream_request_->encodeData(2048, false);
    upstream_request_->encodeTrailers(any_response_trailers_);
    ASSERT_TRUE(response_decoder->waitForEndStream());
  }

  Http::TestRequestHeaderMapImpl request_headers_post_{
      {":method", "POST"}, {":scheme", "http"}, {":authority", "test"}, {":path", "/test"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"}};

  Http::TestRequestTrailerMapImpl any_request_trailers_{{"x-foo", "foo"}};
  Http::TestResponseTrailerMapImpl any_response_trailers_{{"x-bar", "bar"}};
};

TEST_F(BandwidthShareIntegrationTest, DisabledJustPassesThrough) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initializeFilter(disabledFilter());
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  ASSERT_NE(nullptr, upstream_request_->trailers());
  EXPECT_THAT(*upstream_request_->trailers(), ContainsHeader("x-foo", "foo"));
  upstream_request_->encodeHeaders(response_headers_, false);
  upstream_request_->encodeData(2048, false);
  upstream_request_->encodeTrailers(any_response_trailers_);
  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_NE(nullptr, response_decoder->trailers());
  EXPECT_THAT(*response_decoder->trailers(), ContainsHeader("x-bar", "bar"));

  EXPECT_THAT(test_server_->counters(),
              Not(HasMetric(MetricNameMatches(StartsWith("bandwidth_share.")))));
  EXPECT_THAT(test_server_->gauges(),
              Not(HasMetric(MetricNameMatches(StartsWith("bandwidth_share.")))));
}

TEST_F(BandwidthShareIntegrationTest, LimitCausesPausesBothWays) {
  initializeFilter(twoWay1kFilter());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.bucket1.tenant..direction.request";
  const std::string response_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.bucket2.tenant..direction.response";
  const std::string request_streams_currently_limited =
      "bandwidth_share.streams_currently_limited.bucket_id.bucket1.tenant..direction.request";
  const std::string response_streams_currently_limited =
      "bandwidth_share.streams_currently_limited.bucket_id.bucket2.tenant..direction.response";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  EXPECT_EQ(1, gaugeValue(request_streams_currently_limited));
  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeData(2048, false);
  upstream_request_->encodeTrailers(any_response_trailers_);
  test_server_->waitForGauge(response_bytes_pending, Ge(1));
  EXPECT_EQ(1, gaugeValue(response_streams_currently_limited));
  test_server_->waitForGauge(response_bytes_pending, Eq(0));
  ASSERT_TRUE(response_decoder->waitForEndStream());

  EXPECT_GT(
      counterValue("bandwidth_share.bytes.bucket_id.bucket1.tenant..direction.request.handling."
                   "limited"),
      512);
  EXPECT_GT(
      counterValue("bandwidth_share.bytes.bucket_id.bucket2.tenant..direction.response.handling."
                   "limited"),
      512);
  EXPECT_EQ(0, gaugeValue(request_streams_currently_limited));
  EXPECT_EQ(0, gaugeValue(response_streams_currently_limited));
  EXPECT_EQ(0, gaugeValue(request_bytes_pending));
  EXPECT_EQ(0, gaugeValue(response_bytes_pending));
}

TEST_F(BandwidthShareIntegrationTest, ResetWhileRequestBytesBufferedCleansStats) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  initializeFilter(runtimeControlledRequestFilter());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.runtime_bucket.tenant..direction.request";
  const std::string request_streams_currently_limited =
      "bandwidth_share.streams_currently_limited.bucket_id.runtime_bucket.tenant..direction."
      "request";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  EXPECT_EQ(1, gaugeValue(request_streams_currently_limited));

  codec_client_->sendReset(request_encoder);
  ASSERT_TRUE(response_decoder->waitForReset());
  ASSERT_TRUE(upstream_request_->waitForReset(*dispatcher_));

  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  EXPECT_EQ(0, gaugeValue(request_streams_currently_limited));
}

TEST_F(BandwidthShareIntegrationTest, ResetWhileResponseBytesBufferedCleansStats) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  initializeFilter(responseOnly1kFilter());
  const std::string response_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.response_bucket.tenant..direction.response";
  const std::string response_streams_currently_limited =
      "bandwidth_share.streams_currently_limited.bucket_id.response_bucket.tenant..direction."
      "response";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(1, 'a'), true);
  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeData(2048, false);
  test_server_->waitForGauge(response_bytes_pending, Ge(1));
  EXPECT_EQ(1, gaugeValue(response_streams_currently_limited));

  upstream_request_->encodeResetStream();
  ASSERT_TRUE(response_decoder->waitForReset());

  test_server_->waitForGauge(response_bytes_pending, Eq(0));
  EXPECT_EQ(0, gaugeValue(response_streams_currently_limited));
}

TEST_F(BandwidthShareIntegrationTest, RuntimeLimitChangesApplyToNewStreams) {
  config_helper_.addRuntimeOverride("bandwidth_share.runtime_request_kbps", "0");
  initializeFilter(runtimeControlledRequestFilter());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.runtime_bucket.tenant..direction.request";
  const std::string request_streams_currently_limited =
      "bandwidth_share.streams_currently_limited.bucket_id.runtime_bucket.tenant..direction."
      "request";
  const std::string request_bytes_limited =
      "bandwidth_share.bytes.bucket_id.runtime_bucket.tenant..direction.request.handling.limited";
  makeDownstreamConnection();
  sendRequestAndResponse();
  EXPECT_THAT(test_server_->counters(),
              Not(HasMetric(MetricNameMatches(StartsWith("bandwidth_share.")))));
  EXPECT_THAT(test_server_->gauges(),
              Not(HasMetric(MetricNameMatches(StartsWith("bandwidth_share.")))));

  ASSERT_OK(setRuntimeInt("bandwidth_share.runtime_request_kbps", 1));

  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  EXPECT_EQ(1, gaugeValue(request_streams_currently_limited));
  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, true);
  ASSERT_TRUE(response_decoder->waitForEndStream());

  const uint64_t limited_bytes = counterValue(request_bytes_limited);
  EXPECT_GT(limited_bytes, 512);
  EXPECT_EQ(0, gaugeValue(request_streams_currently_limited));
  EXPECT_EQ(0, gaugeValue(request_bytes_pending));

  ASSERT_OK(setRuntimeInt("bandwidth_share.runtime_request_kbps", 0));

  sendRequestAndResponse();

  EXPECT_EQ(limited_bytes, counterValue(request_bytes_limited));
  EXPECT_EQ(0, gaugeValue(request_streams_currently_limited));
  EXPECT_EQ(0, gaugeValue(request_bytes_pending));
}

TEST_F(BandwidthShareIntegrationTest, PendingResponseBytesDecreaseWhileStillBuffered) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  initializeFilter(responseOnly1kFilter());
  const std::string response_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.response_bucket.tenant..direction.response";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(1, 'a'), true);
  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeData(4096, false);

  test_server_->waitForGauge(response_bytes_pending, Ge(2048));
  Event::TestTimeSystem::RealTimeBound bound(TestUtility::DefaultTimeout);
  test_server_->waitForGauge(response_bytes_pending, Lt(1024));
  EXPECT_GT(gaugeValue(response_bytes_pending), 0);

  upstream_request_->encodeData(1, true);
  test_server_->waitForGauge(response_bytes_pending, Eq(0));
  ASSERT_TRUE(response_decoder->waitForEndStream());
}

TEST_F(BandwidthShareIntegrationTest, RouteConfigCanLimitAndTagTenantStats) {
  initializeFilterWithRouteConfig(disabledFilter(), tenantTaggedRouteConfig());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.route_bucket.tenant.gold.direction.request";
  const std::string request_streams_currently_limited =
      "bandwidth_share.streams_currently_limited.bucket_id.route_bucket.tenant.gold.direction."
      "request";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  EXPECT_EQ(1, gaugeValue(request_streams_currently_limited));
  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, true);
  ASSERT_TRUE(response_decoder->waitForEndStream());

  EXPECT_GT(
      counterValue("bandwidth_share.bytes.bucket_id.route_bucket.tenant.gold.direction.request."
                   "handling.limited"),
      512);
  EXPECT_EQ(0, gaugeValue(request_streams_currently_limited));
  EXPECT_EQ(0, gaugeValue(request_bytes_pending));
}

TEST_F(BandwidthShareIntegrationTest, DoesNotAddEmptyResponseTrailersWithoutDelay) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  initializeFilter(responseTrailersWithoutDelayFilter());
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, "a", true);
  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeData(1, true);
  ASSERT_TRUE(response_decoder->waitForEndStream());

  EXPECT_EQ(nullptr, response_decoder->trailers());
}

TEST_F(BandwidthShareIntegrationTest, AddsResponseTrailersAfterLimitedResponse) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  initializeFilter(responseTrailersFilter());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.trailer_request.tenant..direction.request";
  const std::string response_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.trailer_response.tenant..direction.response";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeData(2048, true);
  test_server_->waitForGauge(response_bytes_pending, Ge(1));
  test_server_->waitForGauge(response_bytes_pending, Eq(0));
  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_NE(nullptr, response_decoder->trailers());

  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-duration-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-duration-ms", _));
}

TEST_F(BandwidthShareIntegrationTest, AddsResponseTrailersToUpstreamTrailers) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initializeFilter(responseTrailersFilter());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.trailer_request.tenant..direction.request";
  const std::string response_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.trailer_response.tenant..direction.response";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeData(2048, false);
  test_server_->waitForGauge(response_bytes_pending, Ge(1));
  test_server_->waitForGauge(response_bytes_pending, Eq(0));
  upstream_request_->encodeTrailers(any_response_trailers_);
  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_NE(nullptr, response_decoder->trailers());

  EXPECT_THAT(*response_decoder->trailers(), ContainsHeader("x-bar", "bar"));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-duration-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-duration-ms", _));
}

TEST_F(BandwidthShareIntegrationTest, PausesUpstreamTrailersBehindBufferedResponse) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initializeFilter(responseTrailersFilter());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.trailer_request.tenant..direction.request";
  const std::string response_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.trailer_response.tenant..direction.response";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeData(2048, false);
  test_server_->waitForGauge(response_bytes_pending, Ge(1));
  upstream_request_->encodeTrailers(any_response_trailers_);
  test_server_->waitForGauge(response_bytes_pending, Eq(0));
  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_NE(nullptr, response_decoder->trailers());

  EXPECT_THAT(*response_decoder->trailers(), ContainsHeader("x-bar", "bar"));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-duration-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-duration-ms", _));
}

TEST_F(BandwidthShareIntegrationTest, AddsResponseTrailersWhenUpstreamTrailersDoNotPause) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initializeFilter(responseTrailersFilter());
  const std::string request_bytes_pending =
      "bandwidth_share.bytes_pending.bucket_id.trailer_request.tenant..direction.request";
  makeDownstreamConnection();
  auto [request_encoder, response_decoder] = codec_client_->startRequest(request_headers_post_);
  codec_client_->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client_->sendTrailers(request_encoder, any_request_trailers_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  test_server_->waitForGauge(request_bytes_pending, Ge(1));
  test_server_->waitForGauge(request_bytes_pending, Eq(0));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  response_decoder->waitForHeaders();
  upstream_request_->encodeTrailers(any_response_trailers_);
  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_NE(nullptr, response_decoder->trailers());

  EXPECT_THAT(*response_decoder->trailers(), ContainsHeader("x-bar", "bar"));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-delay-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-request-duration-ms", _));
  EXPECT_THAT(*response_decoder->trailers(),
              ContainsHeader("x-test-bandwidth-response-duration-ms", _));
}

} // namespace
} // namespace Envoy
