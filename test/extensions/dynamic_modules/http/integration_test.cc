#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/common/base64.h"

#include "test/integration/http_integration.h"

namespace Envoy {
class DynamicModulesIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  DynamicModulesIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {};

  void
  initializeFilter(const std::string& filter_name, const std::string& config = "",
                   const std::string& per_route_config = "",
                   const std::string& type_url = "type.googleapis.com/google.protobuf.StringValue",
                   bool upstream_filter = false) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    constexpr auto filter_config = R"EOF(
name: envoy.extensions.filters.http.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
  dynamic_module_config:
    name: http_integration_test
  filter_name: {}
  filter_config:
    "@type": {}
    value: {}
)EOF";

    if (!per_route_config.empty()) {
      constexpr auto filter_per_route_config = R"EOF(
dynamic_module_config:
  name: http_integration_test
per_route_config_name: {}
filter_config:
  "@type": {}
  value: {}
)EOF";
      envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute
          per_route_config_proto;
      TestUtility::loadFromYaml(
          fmt::format(filter_per_route_config, filter_name, type_url, per_route_config),
          per_route_config_proto);

      config_helper_.addConfigModifier(
          [per_route_config_proto](envoy::extensions::filters::network::http_connection_manager::
                                       v3::HttpConnectionManager& cfg) {
            auto* config = cfg.mutable_route_config()
                               ->mutable_virtual_hosts()
                               ->Mutable(0)
                               ->mutable_typed_per_filter_config();

            (*config)["envoy.extensions.filters.http.dynamic_modules"].PackFrom(
                per_route_config_proto);
          });
    }

    config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
    config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
    config_helper_.prependFilter(fmt::format(filter_config, filter_name, type_url, config),
                                 !upstream_filter);
    initialize();
  }
  void runHeaderCallbacksTest(bool upstream_filter) {
    initializeFilter("header_callbacks", "dog:cat", "",
                     "type.googleapis.com/google.protobuf.StringValue", upstream_filter);
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

    Http::TestRequestHeaderMapImpl request_headers{{"foo", "bar"},
                                                   {":method", "POST"},
                                                   {":path", "/test/long/url"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"}};
    Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
    Http::TestResponseTrailerMapImpl response_trailers{{"foo", "bar"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(encoder_decoder.first, 10, false);
    codec_client_->sendTrailers(encoder_decoder.first, request_trailers);

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, false);
    upstream_request_->encodeData(10, false);
    upstream_request_->encodeTrailers(response_trailers);

    ASSERT_TRUE(response->waitForEndStream());

    // Verify the proxied request was received upstream, as expected.
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(10U, upstream_request_->bodyLength());
    // Verify that the headers/trailers are added as expected.
    EXPECT_EQ(
        "cat",
        upstream_request_->headers().get(Http::LowerCaseString("dog"))[0]->value().getStringView());
    EXPECT_EQ("cat", upstream_request_->trailers()
                         .get()
                         ->get(Http::LowerCaseString("dog"))[0]
                         ->value()
                         .getStringView());
    // Verify the proxied response was received downstream, as expected.
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    EXPECT_EQ(10U, response->body().size());
    // Verify that the headers/trailers are added as expected.
    EXPECT_EQ("cat",
              response->headers().get(Http::LowerCaseString("dog"))[0]->value().getStringView());
    EXPECT_EQ(
        "cat",
        response->trailers().get()->get(Http::LowerCaseString("dog"))[0]->value().getStringView());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesIntegrationTest, PassThrough) {
  initializeFilter("passthrough");

  // Create a client aimed at Envoy’s default HTTP port.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Create some request headers.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Send the request headers from the client, wait until they are received upstream. When they
  // are received, send the default response headers from upstream and wait until they are
  // received at by client
  auto response = sendRequestAndWaitForResponse(request_headers, 10, default_response_headers_, 10);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(10U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(10U, response->body().size());
}

TEST_P(DynamicModulesIntegrationTest, HeaderCallbacks) { runHeaderCallbacksTest(false); }

TEST_P(DynamicModulesIntegrationTest, HeaderCallbacksWithUpstreamFilter) {
  runHeaderCallbacksTest(true);
}

TEST_P(DynamicModulesIntegrationTest, BytesConfig) {
  initializeFilter("header_callbacks", "ZG9nOmNhdA==" /* echo -n "dog:cat" | base64 */, "",
                   "type.googleapis.com/google.protobuf.BytesValue");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{{"foo", "bar"},
                                                 {":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};
  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"foo", "bar"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(encoder_decoder.first, 10, false);
  codec_client_->sendTrailers(encoder_decoder.first, request_trailers);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(10, false);
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(10U, upstream_request_->bodyLength());
  // Verify that the headers/trailers are added as expected.
  EXPECT_EQ(
      "cat",
      upstream_request_->headers().get(Http::LowerCaseString("dog"))[0]->value().getStringView());
}

TEST_P(DynamicModulesIntegrationTest, PerRouteConfig) {
  initializeFilter("per_route_config", "a", "b");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{{"foo", "bar"},
                                                 {":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  // Verify that the headers/trailers are added as expected.
  EXPECT_EQ("a", upstream_request_->headers()
                     .get(Http::LowerCaseString("x-config"))[0]
                     ->value()
                     .getStringView());
  EXPECT_EQ("b", upstream_request_->headers()
                     .get(Http::LowerCaseString("x-per-route-config"))[0]
                     ->value()
                     .getStringView());
}

TEST_P(DynamicModulesIntegrationTest, BodyCallbacks) {
  initializeFilter("body_callbacks");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "test.com"}};
  auto encoder_decoder = codec_client_->startRequest(request_headers, false);
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(encoder_decoder.first, "request", false);
  codec_client_->sendData(encoder_decoder.first, "_b", false);
  codec_client_->sendData(encoder_decoder.first, "ody", true);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData("res", false);
  upstream_request_->encodeData("ponse", false);
  upstream_request_->encodeData("_body", true);

  ASSERT_TRUE(response->waitForEndStream());

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("new_request_body", upstream_request_->body().toString());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("new_response_body", response->body());
}

TEST_P(DynamicModulesIntegrationTest, BodyCallbacks_WithoutBuffering) {
  initializeFilter("body_callbacks", "immediate_end_of_stream");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, "request_body");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData("response_body", true);

  ASSERT_TRUE(response->waitForEndStream());

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("new_request_body", upstream_request_->body().toString());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("new_response_body", response->body());
}

TEST_P(DynamicModulesIntegrationTest, SendResponseFromOnRequestHeaders) {
  initializeFilter("send_response", "on_request_headers");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  auto body = response->body();
  EXPECT_EQ("local_response_body_from_on_request_headers", body);
  EXPECT_EQ(
      "some_value",
      response->headers().get(Http::LowerCaseString("some_header"))[0]->value().getStringView());
}

TEST_P(DynamicModulesIntegrationTest, SendResponseFromOnRequestBody) {
  initializeFilter("send_response", "on_request_body");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(encoder_decoder.first, 10, true);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  auto body = response->body();
  EXPECT_EQ("local_response_body_from_on_request_body", body);
  EXPECT_EQ(
      "some_value",
      response->headers().get(Http::LowerCaseString("some_header"))[0]->value().getStringView());
}

TEST_P(DynamicModulesIntegrationTest, SendResponseFromOnResponseHeaders) {
  initializeFilter("send_response", "on_response_headers");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_, true);
  auto response = std::move(encoder_decoder.second);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().Status()->value().getStringView());
  auto body = response->body();
  EXPECT_EQ("local_response_body_from_on_response_headers", body);
  EXPECT_EQ(
      "some_value",
      response->headers().get(Http::LowerCaseString("some_header"))[0]->value().getStringView());
}

TEST_P(DynamicModulesIntegrationTest, HttpCalloutsNonExistentCluster) {
  initializeFilter("http_callouts", "missing");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().Status()->value().getStringView());
  EXPECT_EQ("bar",
            response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
}

TEST_P(DynamicModulesIntegrationTest, HttpCalloutsOK) {
  initializeFilter("http_callouts", "cluster_0");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  waitForNextUpstreamRequest();

  Http::TestRequestHeaderMapImpl response_headers{{"some_header", "some_value"},
                                                  {":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData("response_body_from_callout", true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("local_response_body", response->body());
}

TEST_P(DynamicModulesIntegrationTest, Scheduler) {
  initializeFilter("http_filter_scheduler");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_, true);
  auto response = std::move(encoder_decoder.second);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(DynamicModulesIntegrationTest, FakeExternalCache) {
  initializeFilter("fake_external_cache");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Non existent cache key should return 200 OK with body.
  {
    auto headers = default_request_headers_;
    headers.addCopy(Http::LowerCaseString("cacahe-key"), "non-existent");
    auto encoder_decoder = codec_client_->startRequest(headers, true);
    auto response = std::move(encoder_decoder.second);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    EXPECT_EQ("req", upstream_request_->headers()
                         .get(Http::LowerCaseString("on-scheduled"))[0]
                         ->value()
                         .getStringView());
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    EXPECT_EQ(
        "res",
        response->headers().get(Http::LowerCaseString("on-scheduled"))[0]->value().getStringView());
    EXPECT_TRUE(response->body().empty());
  }
  // Existing cache key should return 200 OK with body and shouldn't reach the upstream.
  {
    auto headers = default_request_headers_;
    headers.addCopy(Http::LowerCaseString("cacahe-key"), "existing");
    auto encoder_decoder = codec_client_->startRequest(headers, true);
    auto response = std::move(encoder_decoder.second);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    EXPECT_EQ("yes",
              response->headers().get(Http::LowerCaseString("cached"))[0]->value().getStringView());
    EXPECT_EQ("cached_response_body", response->body());
  }
}

TEST_P(DynamicModulesIntegrationTest, StatsCallbacks) {
  initializeFilter("stats_callbacks", "header_to_count,header_to_set");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // End-to-end request
  {
    Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
    request_headers.addCopy(Http::LowerCaseString("header_to_count"), "3");
    request_headers.addCopy(Http::LowerCaseString("header_to_set"), "100");
    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);
    waitForNextUpstreamRequest();
    test_server_->waitUntilHistogramHasSamples("dynamicmodulescustom.requests_header_values");

    EXPECT_EQ(test_server_->counter("dynamicmodulescustom.requests_total")->value(), 1);
    EXPECT_EQ(test_server_->gauge("dynamicmodulescustom.requests_pending")->value(), 1);
    EXPECT_EQ(test_server_->gauge("dynamicmodulescustom.requests_set_value")->value(), 100);
    auto requests_header_values =
        test_server_->histogram("dynamicmodulescustom.requests_header_values");
    EXPECT_EQ(
        TestUtility::readSampleCount(test_server_->server().dispatcher(), *requests_header_values),
        1);
    EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                          *requests_header_values)),
              3);

    EXPECT_EQ(
        test_server_
            ->counter(
                "dynamicmodulescustom.entrypoint_total.entrypoint.on_request_headers.method.GET")
            ->value(),
        1);
    EXPECT_EQ(
        test_server_
            ->gauge(
                "dynamicmodulescustom.entrypoint_pending.entrypoint.on_request_headers.method.GET")
            ->value(),
        1);
    EXPECT_EQ(test_server_
                  ->gauge("dynamicmodulescustom.entrypoint_set_value.entrypoint.on_request_headers."
                          "method.GET")
                  ->value(),
              100);
    auto request_entrypoint_header_values = test_server_->histogram(
        "dynamicmodulescustom.entrypoint_header_values.entrypoint.on_request_headers.method.GET");
    EXPECT_EQ(TestUtility::readSampleCount(test_server_->server().dispatcher(),
                                           *request_entrypoint_header_values),
              1);
    EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                          *request_entrypoint_header_values)),
              3);

    Http::TestResponseHeaderMapImpl response_headers = default_response_headers_;
    response_headers.addCopy(Http::LowerCaseString("header_to_count"), "3");
    response_headers.addCopy(Http::LowerCaseString("header_to_set"), "999");
    upstream_request_->encodeHeaders(response_headers, false);
    response->waitForHeaders();
    test_server_->waitUntilHistogramHasSamples(
        "dynamicmodulescustom.entrypoint_header_values.entrypoint.on_response_headers.method.GET");

    EXPECT_EQ("200", response->headers().Status()->value().getStringView());

    EXPECT_EQ(
        test_server_
            ->counter(
                "dynamicmodulescustom.entrypoint_total.entrypoint.on_response_headers.method.GET")
            ->value(),
        1);
    EXPECT_EQ(
        test_server_
            ->gauge(
                "dynamicmodulescustom.entrypoint_pending.entrypoint.on_response_headers.method.GET")
            ->value(),
        1);
    EXPECT_EQ(test_server_
                  ->gauge("dynamicmodulescustom.entrypoint_set_value.entrypoint.on_response_"
                          "headers.method.GET")
                  ->value(),
              999);
    auto response_entrypoint_header_values = test_server_->histogram(
        "dynamicmodulescustom.entrypoint_header_values.entrypoint.on_response_headers.method.GET");
    EXPECT_EQ(TestUtility::readSampleCount(test_server_->server().dispatcher(),
                                           *response_entrypoint_header_values),
              1);
    EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                          *response_entrypoint_header_values)),
              3);

    Buffer::OwnedImpl response_data("goodbye");
    upstream_request_->encodeData(response_data, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    EXPECT_EQ(
        test_server_
            ->gauge(
                "dynamicmodulescustom.entrypoint_pending.entrypoint.on_response_headers.method.GET")
            ->value(),
        0);

    // Check if stats preserved within filter
    EXPECT_EQ(
        test_server_
            ->counter(
                "dynamicmodulescustom.entrypoint_total.entrypoint.on_request_headers.method.GET")
            ->value(),
        1);
    EXPECT_EQ(
        test_server_
            ->gauge(
                "dynamicmodulescustom.entrypoint_pending.entrypoint.on_request_headers.method.GET")
            ->value(),
        0);
    EXPECT_EQ(test_server_
                  ->gauge("dynamicmodulescustom.entrypoint_set_value.entrypoint.on_request_headers."
                          "method.GET")
                  ->value(),
              100);
    EXPECT_EQ(TestUtility::readSampleCount(test_server_->server().dispatcher(),
                                           *request_entrypoint_header_values),
              1);
    EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                          *request_entrypoint_header_values)),
              3);
    EXPECT_EQ(
        test_server_
            ->counter(
                "dynamicmodulescustom.entrypoint_total.entrypoint.on_response_headers.method.GET")
            ->value(),
        1);
    EXPECT_EQ(test_server_
                  ->gauge("dynamicmodulescustom.entrypoint_set_value.entrypoint.on_response_"
                          "headers.method.GET")
                  ->value(),
              999);
    EXPECT_EQ(TestUtility::readSampleCount(test_server_->server().dispatcher(),
                                           *response_entrypoint_header_values),
              1);
    EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                          *response_entrypoint_header_values)),
              3);
  }

  // Test stat values persisted after filter is destroyed
  {
    Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
    request_headers.addCopy(Http::LowerCaseString("header_to_count"), "13");
    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);
    waitForNextUpstreamRequest();
    test_server_->waitForNumHistogramSamplesGe("dynamicmodulescustom.requests_header_values", 2);

    EXPECT_EQ(test_server_->counter("dynamicmodulescustom.requests_total")->value(), 2);
    EXPECT_EQ(test_server_->gauge("dynamicmodulescustom.requests_pending")->value(), 1);
    EXPECT_EQ(test_server_->gauge("dynamicmodulescustom.requests_set_value")->value(),
              100); // set above in first request
    auto requests_header_values =
        test_server_->histogram("dynamicmodulescustom.requests_header_values");
    EXPECT_EQ(
        TestUtility::readSampleCount(test_server_->server().dispatcher(), *requests_header_values),
        2);
    EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                          *requests_header_values)),
              3 + 13);

    EXPECT_EQ(
        test_server_
            ->counter(
                "dynamicmodulescustom.entrypoint_total.entrypoint.on_request_headers.method.GET")
            ->value(),
        2);
    EXPECT_EQ(
        test_server_
            ->gauge(
                "dynamicmodulescustom.entrypoint_pending.entrypoint.on_request_headers.method.GET")
            ->value(),
        1);
    EXPECT_EQ(test_server_
                  ->gauge("dynamicmodulescustom.entrypoint_set_value.entrypoint.on_request_headers."
                          "method.GET")
                  ->value(),
              100); // set above in first request
    auto request_entrypoint_header_values = test_server_->histogram(
        "dynamicmodulescustom.entrypoint_header_values.entrypoint.on_request_headers.method.GET");
    EXPECT_EQ(TestUtility::readSampleCount(test_server_->server().dispatcher(),
                                           *request_entrypoint_header_values),
              2);
    EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                          *request_entrypoint_header_values)),
              3 + 13);

    Http::TestResponseHeaderMapImpl response_headers = default_response_headers_;
    response_headers.addCopy(Http::LowerCaseString("header_to_count"), "5");
    response_headers.addCopy(Http::LowerCaseString("header_to_set"), "1000");
    upstream_request_->encodeHeaders(response_headers, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
  }
}

TEST_P(DynamicModulesIntegrationTest, InjectBody) {
  initializeFilter("inject_body");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto response =
      codec_client_->makeRequestWithBody(default_request_headers_, "ignored_request_body");
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData("ignored_response_body", true);

  ASSERT_TRUE(response->waitForEndStream());

  // Verify the injected request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("injected_request_body", upstream_request_->body().toString());
  // Verify the injected response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("injected_response_body", response->body());
}

} // namespace Envoy
