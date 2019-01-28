#include "envoy/data/tap/v2alpha/wrapper.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TapIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  TapIntegrationTest()
      // Note: This test must use HTTP/2 because of the lack of early close detection for
      // HTTP/1 on OSX. In this test we close the admin /tap stream when we don't want any
      // more data, and without immediate close detection we can't have a flake free test.
      // Thus, we use HTTP/2 for everything here.
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam(), realTime()) {}

  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);
    initialize();
  }

  const envoy::api::v2::core::HeaderValue*
  findHeader(const std::string& key,
             const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>& headers) {
    for (const auto& header : headers) {
      if (header.key() == key) {
        return &header;
      }
    }

    return nullptr;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TapIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify a basic tap flow using the admin handler.
TEST_P(TapIntegrationTest, AdminBasicFlow) {
  const std::string FILTER_CONFIG =
      R"EOF(
name: envoy.filters.http.tap
config:
  admin_config:
    config_id: test_config_id
)EOF";

  initializeFilter(FILTER_CONFIG);

  // Initial request/response with no tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestHeaderMapImpl request_headers_tap{{":method", "GET"},
                                                    {":path", "/"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"foo", "bar"}};
  IntegrationStreamDecoderPtr decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  const Http::TestHeaderMapImpl response_headers_no_tap{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    or_match:
      rules:
        - http_request_match:
            headers:
              - name: foo
                exact_match: bar
        - http_response_match:
            headers:
              - name: bar
                exact_match: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  // Setup a tap and disconnect it without any request/response.
  IntegrationCodecClientPtr admin_client_ =
      makeHttpConnection(makeClientConnection(lookupPort("admin")));
  const Http::TestHeaderMapImpl admin_request_headers{
      {":method", "POST"}, {":path", "/tap"}, {":scheme", "http"}, {":authority", "host"}};
  IntegrationStreamDecoderPtr admin_response =
      admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml);
  admin_response->waitForHeaders();
  EXPECT_STREQ("200", admin_response->headers().Status()->value().c_str());
  EXPECT_FALSE(admin_response->complete());
  admin_client_->close();
  test_server_->waitForGaugeEq("http.admin.downstream_rq_active", 0);

  // Second request/response with no tap.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  // Setup the tap again and leave it open.
  admin_client_ = makeHttpConnection(makeClientConnection(lookupPort("admin")));
  admin_response = admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml);
  admin_response->waitForHeaders();
  EXPECT_STREQ("200", admin_response->headers().Status()->value().c_str());
  EXPECT_FALSE(admin_response->complete());

  // Do a request which should tap, matching on request headers.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  // Wait for the tap message.
  admin_response->waitForBodyData(1);
  envoy::data::tap::v2alpha::BufferedTraceWrapper trace;
  MessageUtil::loadFromYaml(admin_response->body(), trace);
  EXPECT_EQ(trace.http_buffered_trace().request_headers().size(), 8);
  EXPECT_EQ(trace.http_buffered_trace().response_headers().size(), 5);
  admin_response->clearBody();

  // Do a request which should not tap.
  const Http::TestHeaderMapImpl request_headers_no_tap{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_no_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  // Do a request which should tap, matching on response headers.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_no_tap);
  waitForNextUpstreamRequest();
  const Http::TestHeaderMapImpl response_headers_tap{{":status", "200"}, {"bar", "baz"}};
  upstream_request_->encodeHeaders(response_headers_tap, true);
  decoder->waitForEndStream();

  // Wait for the tap message.
  admin_response->waitForBodyData(1);
  MessageUtil::loadFromYaml(admin_response->body(), trace);
  EXPECT_EQ(trace.http_buffered_trace().request_headers().size(), 7);
  EXPECT_EQ(
      "http",
      findHeader("x-forwarded-proto", trace.http_buffered_trace().request_headers())->value());
  EXPECT_EQ(trace.http_buffered_trace().response_headers().size(), 6);
  EXPECT_NE(nullptr, findHeader("date", trace.http_buffered_trace().response_headers()));
  EXPECT_EQ("baz", findHeader("bar", trace.http_buffered_trace().response_headers())->value());

  admin_client_->close();
  test_server_->waitForGaugeEq("http.admin.downstream_rq_active", 0);

  // Now setup a tap that matches on logical AND.
  const std::string admin_request_yaml2 =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    and_match:
      rules:
        - http_request_match:
            headers:
              - name: foo
                exact_match: bar
        - http_response_match:
            headers:
              - name: bar
                exact_match: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  admin_client_ = makeHttpConnection(makeClientConnection(lookupPort("admin")));
  admin_response = admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml2);
  admin_response->waitForHeaders();
  EXPECT_STREQ("200", admin_response->headers().Status()->value().c_str());
  EXPECT_FALSE(admin_response->complete());

  // Do a request that matches, but the response does not match. No tap.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  // Do a request that doesn't match, but the response does match. No tap.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_no_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_tap, true);
  decoder->waitForEndStream();

  // Do a request that matches and a response that matches. Should tap.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_tap, true);
  decoder->waitForEndStream();

  // Wait for the tap message.
  admin_response->waitForBodyData(1);
  MessageUtil::loadFromYaml(admin_response->body(), trace);

  admin_client_->close();
  EXPECT_EQ(3UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

} // namespace
} // namespace Envoy
