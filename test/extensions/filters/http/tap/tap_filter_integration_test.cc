#include "envoy/data/tap/v2alpha/wrapper.pb.h"

#include "test/integration/http_integration.h"

#include "absl/strings/match.h"
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

  void makeRequest(const Http::TestHeaderMapImpl& request_headers,
                   const Http::TestHeaderMapImpl& response_headers) {
    IntegrationStreamDecoderPtr decoder = codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, true);
    decoder->waitForEndStream();
  }

  const Http::TestHeaderMapImpl request_headers_tap_{{":method", "GET"},
                                                     {":path", "/"},
                                                     {":scheme", "http"},
                                                     {":authority", "host"},
                                                     {"foo", "bar"}};

  const Http::TestHeaderMapImpl request_headers_no_tap_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  const Http::TestHeaderMapImpl response_headers_tap_{{":status", "200"}, {"bar", "baz"}};

  const Http::TestHeaderMapImpl response_headers_no_tap_{{":status", "200"}};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TapIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify a static configuration with an any matcher, writing to a file per tap sink.
TEST_P(TapIntegrationTest, StaticFilePerTap) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.tap
config:
  common_config:
    static_config:
      match_config:
        any_match: true
      output_config:
        sinks:
          - file_per_tap:
              path_prefix: {}
)EOF";

  const std::string path_prefix =
      TestEnvironment::temporaryDirectory() + "/tap_integration_static_file/";
  TestEnvironment::createPath(path_prefix);
  initializeFilter(fmt::format(filter_config, path_prefix));

  // Initial request/response with tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, response_headers_no_tap_);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  // Find the written .pb file and verify it.
  auto files = TestUtility::listFiles(path_prefix, false);
  auto pb_file = std::find_if(files.begin(), files.end(),
                              [](const std::string& s) { return absl::EndsWith(s, ".pb"); });
  ASSERT_NE(pb_file, files.end());

  envoy::data::tap::v2alpha::BufferedTraceWrapper trace;
  MessageUtil::loadFromFile(*pb_file, trace, *api_);
  EXPECT_TRUE(trace.has_http_buffered_trace());
}

// Verify a basic tap flow using the admin handler.
TEST_P(TapIntegrationTest, AdminBasicFlow) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.tap
config:
  common_config:
    admin_config:
      config_id: test_config_id
)EOF";

  initializeFilter(filter_config);

  // Initial request/response with no tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, response_headers_no_tap_);

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
  makeRequest(request_headers_tap_, response_headers_no_tap_);

  // Setup the tap again and leave it open.
  admin_client_ = makeHttpConnection(makeClientConnection(lookupPort("admin")));
  admin_response = admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml);
  admin_response->waitForHeaders();
  EXPECT_STREQ("200", admin_response->headers().Status()->value().c_str());
  EXPECT_FALSE(admin_response->complete());

  // Do a request which should tap, matching on request headers.
  makeRequest(request_headers_tap_, response_headers_no_tap_);

  // Wait for the tap message.
  admin_response->waitForBodyData(1);
  envoy::data::tap::v2alpha::BufferedTraceWrapper trace;
  MessageUtil::loadFromYaml(admin_response->body(), trace);
  EXPECT_EQ(trace.http_buffered_trace().request_headers().size(), 8);
  EXPECT_EQ(trace.http_buffered_trace().response_headers().size(), 5);
  admin_response->clearBody();

  // Do a request which should not tap.
  makeRequest(request_headers_no_tap_, response_headers_no_tap_);

  // Do a request which should tap, matching on response headers.
  makeRequest(request_headers_no_tap_, response_headers_tap_);

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
  makeRequest(request_headers_tap_, response_headers_no_tap_);

  // Do a request that doesn't match, but the response does match. No tap.
  makeRequest(request_headers_no_tap_, response_headers_tap_);

  // Do a request that matches and a response that matches. Should tap.
  makeRequest(request_headers_tap_, response_headers_tap_);

  // Wait for the tap message.
  admin_response->waitForBodyData(1);
  MessageUtil::loadFromYaml(admin_response->body(), trace);

  admin_client_->close();
  EXPECT_EQ(3UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

} // namespace
} // namespace Envoy
