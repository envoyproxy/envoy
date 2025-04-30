#include "source/common/common/base64.h"

#include "test/integration/http_integration.h"

namespace Envoy {
class DynamicModulesIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  DynamicModulesIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {};

  void
  initializeFilter(const std::string& filter_name, const std::string& config = "",
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

    config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
    config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
    config_helper_.prependFilter(fmt::format(filter_config, filter_name, type_url, config),
                                 !upstream_filter);
    initialize();
  }
  void runHeaderCallbacksTest(bool upstream_filter) {
    initializeFilter("header_callbacks", "dog:cat",
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
  initializeFilter("header_callbacks", "ZG9nOmNhdA==" /* echo -n "dog:cat" | base64 */,
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

} // namespace Envoy
