#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/common/base64.h"

#include "test/integration/http_integration.h"

namespace Envoy {
class DynamicModulesIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  DynamicModulesIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  };

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

std::string terminal_filter_config;

class DynamicModulesTerminalIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModulesTerminalIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam(), terminal_filter_config) {};

  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    terminal_filter_config = absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        name: envoy.filters.network.http_connection_manager
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          http_filters:
          - name: http_integration_test
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
              dynamic_module_config:
                name: http_integration_test
              filter_name: streaming_terminal_filter
              terminal_filter: true
          route_config:
            virtual_hosts:
            - domains:
              - '*'
              name: local_proxy_route
          stat_prefix: ingress_http
    per_connection_buffer_limit_bytes: 1024
      )EOF");
  }

  void SetUp() override { HttpIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesTerminalIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesTerminalIntegrationTest, StreamingTerminalFilter) {
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  Http::RequestEncoder& request_encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("terminal",
            response->headers().get(Http::LowerCaseString("x-filter"))[0]->value().getStringView());

  response->waitForBodyData(12);
  EXPECT_EQ("Who are you?", response->body());
  response->clearBody();

  auto large_response_chunk = std::string(1024, 'a');
  codec_client_->sendData(request_encoder, "Envoy", false);
  // Have the client read only a chunk at a time to ensure watermarks are
  // triggered.
  for (int i = 0; i < 8; i++) {
    response->waitForBodyData(1024 * (i + 1));
  }
  auto large_response = std::string("");
  for (int i = 0; i < 8; i++) {
    large_response += large_response_chunk;
  }
  EXPECT_EQ(large_response, response->body());
  response->clearBody();

  codec_client_->sendData(request_encoder, "Nope", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("Thanks!", response->body());
  EXPECT_EQ("finished", response->trailers()
                            .get()
                            ->get(Http::LowerCaseString("x-status"))[0]
                            ->value()
                            .getStringView());
  unsigned int above_watermark_count;
  unsigned int below_watermark_count;
  EXPECT_TRUE(absl::SimpleAtoi(response->trailers()
                                   .get()
                                   ->get(Http::LowerCaseString("x-above-watermark-count"))[0]
                                   ->value()
                                   .getStringView(),
                               &above_watermark_count));
  EXPECT_TRUE(absl::SimpleAtoi(response->trailers()
                                   .get()
                                   ->get(Http::LowerCaseString("x-below-watermark-count"))[0]
                                   ->value()
                                   .getStringView(),
                               &below_watermark_count));
  // The filter goes over the watermark count on large response body chunk. With 8 writes, we
  // expect the counts to generally be 8. However, the response flow is executed to completion
  // as soon as the 8th chunk is received by the client. In practice, it is extremely likely
  // for the filter to get 8 above watermark callbacks, and highly likely to get the 8 corresponding
  // below watermark callbacks, but it is conceivable timing issues can cause either to be one
  // lower. Checking 7 or 8 should be a good test while also having no chance of flakiness.
  EXPECT_GE(above_watermark_count, 7);
  EXPECT_LE(above_watermark_count, 8);
  EXPECT_GE(below_watermark_count, 7);
  EXPECT_EQ(below_watermark_count, 8);
}

// Test basic HTTP stream callout. A GET request with streaming response.
TEST_P(DynamicModulesIntegrationTest, HttpStreamBasic) {
  initializeFilter("http_stream_basic", "cluster_0");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  waitForNextUpstreamRequest();

  // Send response headers.
  Http::TestRequestHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);

  // Send response body.
  upstream_request_->encodeData("response_from_upstream", true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("stream_callout_success", response->body());
  EXPECT_EQ(
      "basic",
      response->headers().get(Http::LowerCaseString("x-stream-test"))[0]->value().getStringView());
}

// Test bidirectional HTTP stream callout. A POST request with streaming request and response.
TEST_P(DynamicModulesIntegrationTest, HttpStreamBidirectional) {
  initializeFilter("http_stream_bidirectional", "cluster_0");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  waitForNextUpstreamRequest();

  // Verify the filter sent request data in chunks.
  EXPECT_TRUE(upstream_request_->complete());
  std::string received_body = upstream_request_->body().toString();
  EXPECT_EQ("chunk1chunk2", received_body);

  // Verify trailers were sent.
  EXPECT_TRUE(upstream_request_->trailers().get() != nullptr);

  // Send response with headers, data, and trailers.
  Http::TestRequestHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData("chunk_a", false);
  upstream_request_->encodeData("chunk_b", false);
  Http::TestResponseTrailerMapImpl response_trailers{{"x-response-trailer", "value"}};
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("bidirectional_success", response->body());
  EXPECT_EQ(
      "bidirectional",
      response->headers().get(Http::LowerCaseString("x-stream-test"))[0]->value().getStringView());
  EXPECT_EQ(
      "2",
      response->headers().get(Http::LowerCaseString("x-chunks-sent"))[0]->value().getStringView());
  // Should have received at least 1 data chunk. Due to buffering, the two chunks sent by the
  // upstream may be coalesced into a single chunk by the time they reach the dynamic module.
  EXPECT_GE(std::stoi(std::string(response->headers()
                                      .get(Http::LowerCaseString("x-chunks-received"))[0]
                                      ->value()
                                      .getStringView())),
            1);
}

// Test upstream reset logic.
TEST_P(DynamicModulesIntegrationTest, HttpStreamUpstreamReset) {
  initializeFilter("upstream_reset", "cluster_0");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  waitForNextUpstreamRequest();

  // Send partial response and then reset from upstream to simulate mid-stream failure.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData("partial", false);
  upstream_request_->encodeResetStream();

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("upstream_reset", response->body());
  EXPECT_EQ("true",
            response->headers().get(Http::LowerCaseString("x-reset"))[0]->value().getStringView());
}

TEST_P(DynamicModulesIntegrationTest, ConfigScheduler) {
  initializeFilter("http_config_scheduler");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Poll until the config is updated.
  for (int i = 0; i < 20; ++i) {
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    auto status_header = upstream_request_->headers().get(Http::LowerCaseString("x-test-status"));
    // It should be present.
    ASSERT_FALSE(status_header.empty());
    auto status = status_header[0]->value().getStringView();

    if (status == "true") {
      return;
    }
    absl::SleepFor(absl::Milliseconds(100));
  }
  FAIL() << "Config was not updated in time";
}

// Test buffer limit callbacks for non-terminal filters.
TEST_P(DynamicModulesIntegrationTest, BufferLimitFilter) {
  initializeFilter("buffer_limit_filter");
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  // Verify the buffer limit headers were set by the filter.
  auto initial_limit_header =
      response->headers().get(Http::LowerCaseString("x-initial-buffer-limit"));
  ASSERT_FALSE(initial_limit_header.empty());
  uint64_t initial_limit;
  EXPECT_TRUE(absl::SimpleAtoi(initial_limit_header[0]->value().getStringView(), &initial_limit));

  auto current_limit_header =
      response->headers().get(Http::LowerCaseString("x-current-buffer-limit"));
  ASSERT_FALSE(current_limit_header.empty());
  uint64_t current_limit;
  EXPECT_TRUE(absl::SimpleAtoi(current_limit_header[0]->value().getStringView(), &current_limit));

  // The filter should have either kept the existing limit (if already >= 65536) or increased it.
  // The default buffer limit in Envoy is 16MB (16777216), so the filter should have kept it.
  EXPECT_GE(current_limit, 65536);
  // The initial and current limits should be the same if initial was already >= 65536.
  if (initial_limit >= 65536) {
    EXPECT_EQ(current_limit, initial_limit);
  } else {
    EXPECT_EQ(current_limit, 65536);
  }
}

} // namespace Envoy
