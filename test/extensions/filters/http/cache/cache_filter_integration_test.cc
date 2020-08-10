#include "test/integration/http_protocol_integration.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// TODO(toddmgreer): Expand integration test to include age header values,
// expiration, range headers, HEAD requests, trailers, config customizations,
// cache-control headers, and conditional header fields, as they are
// implemented.

class CacheIntegrationTest : public Event::TestUsingSimulatedTime,
                             public HttpProtocolIntegrationTest {
public:
  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  void initializeFilter(const std::string& config) {
    config_helper_.addFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  const std::string default_config{R"EOF(
    name: "envoy.filters.http.cache"
    typed_config:
        "@type": "type.googleapis.com/envoy.extensions.filters.http.cache.v3alpha.CacheConfig"
        typed_config:
           "@type": "type.googleapis.com/envoy.source.extensions.filters.http.cache.SimpleHttpCacheConfig"
    )EOF"};
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
};

INSTANTIATE_TEST_SUITE_P(Protocols, CacheIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CacheIntegrationTest, MissInsertHit) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  // Set system time to cause Envoy's cached formatted time to match time on this thread.
  simTime().setSystemTime(std::chrono::hours(1));
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
      {":scheme", "http"},
      {":authority", "MissInsertHit"}};
  Http::TestResponseHeaderMapImpl response_headers = {{":status", "200"},
                                                      {"date", formatter_.now(simTime())},
                                                      {"cache-control", "public,max-age=3600"},
                                                      {"content-length", "42"}};

  // Send first request, and get response from upstream.
  {
    IntegrationStreamDecoderPtr request = codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 42 'a's
    upstream_request_->encodeData(42, true);
    // Wait for the response to be read by the codec client.
    request->waitForEndStream();
    EXPECT_TRUE(request->complete());
    EXPECT_THAT(request->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(request->headers().get(Http::Headers::get().Age), nullptr);
    EXPECT_EQ(request->body(), std::string(42, 'a'));
    EXPECT_EQ(waitForAccessLog(access_log_name_),
              fmt::format("- via_upstream{}", TestEnvironment::newLine));
  }

  // Advance time, to verify the original date header is preserved.
  simTime().advanceTimeWait(std::chrono::seconds(10));

  // Send second request, and get response from cache.
  IntegrationStreamDecoderPtr request = codec_client_->makeHeaderOnlyRequest(request_headers);
  request->waitForEndStream();
  EXPECT_TRUE(request->complete());
  EXPECT_THAT(request->headers(), IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(request->body(), std::string(42, 'a'));
  EXPECT_NE(request->headers().get(Http::Headers::get().Age), nullptr);
  // Advance time to force a log flush.
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_EQ(waitForAccessLog(access_log_name_, 1),
            fmt::format("RFCF cache.response_from_cache_filter{}", TestEnvironment::newLine));
}

TEST_P(CacheIntegrationTest, SuccessfulValidation) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  // Set system time to cause Envoy's cached formatted time to match time on this thread.
  simTime().setSystemTime(std::chrono::hours(1));
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
      {":scheme", "http"},
      {":authority", "SuccessfulValidation"}};

  const std::string original_response_date = formatter_.now(simTime());
  Http::TestResponseHeaderMapImpl response_headers = {{":status", "200"},
                                                      {"date", original_response_date},
                                                      {"cache-control", "max-age=0"},
                                                      {"content-length", "42"},
                                                      {"etag", "abc123"}};

  // Send first request, and get response from upstream.
  {
    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 42 'a's
    upstream_request_->encodeData(42, true);
    // Wait for the response to be read by the codec client.
    response_decoder->waitForEndStream();
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::Headers::get().Age), nullptr);
    EXPECT_EQ(response_decoder->body(), std::string(42, 'a'));
    EXPECT_EQ(waitForAccessLog(access_log_name_), "- via_upstream\n");
  }

  simTime().advanceTimeWait(std::chrono::seconds(10));
  const std::string not_modified_date = formatter_.now(simTime());

  // Send second request, the cached response should be validated then served.
  IntegrationStreamDecoderPtr response_decoder =
      codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Check for injected conditional headers -- no "Last-Modified" header so should fallback to
  // "Date".
  Http::TestRequestHeaderMapImpl injected_headers = {{"if-none-match", "abc123"},
                                                     {"if-modified-since", original_response_date}};
  EXPECT_THAT(upstream_request_->headers(), IsSupersetOfHeaders(injected_headers));

  // Create a 304 (not modified) response -> cached response is valid.
  Http::TestResponseHeaderMapImpl not_modified_response_headers = {{":status", "304"},
                                                                   {"date", not_modified_date}};
  upstream_request_->encodeHeaders(not_modified_response_headers, /*end_stream=*/true);

  // The original response headers should be updated with 304 response headers.
  response_headers.setDate(not_modified_date);

  // Wait for the response to be read by the codec client.
  response_decoder->waitForEndStream();

  // Check that the served response is the cached response.
  EXPECT_TRUE(response_decoder->complete());
  EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(response_decoder->body(), std::string(42, 'a'));
  // Check that age header exists as this is a cached response.
  EXPECT_NE(response_decoder->headers().get(Http::Headers::get().Age), nullptr);

  // Advance time to force a log flush.
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_EQ(waitForAccessLog(access_log_name_, 1), "RFCF cache.response_from_cache_filter\n");
}

TEST_P(CacheIntegrationTest, UnsuccessfulValidation) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  // Set system time to cause Envoy's cached formatted time to match time on this thread.
  simTime().setSystemTime(std::chrono::hours(1));
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
      {":scheme", "http"},
      {":authority", "UnsuccessfulValidation"}};

  Http::TestResponseHeaderMapImpl original_response_headers = {{":status", "200"},
                                                               {"date", formatter_.now(simTime())},
                                                               {"cache-control", "max-age=0"},
                                                               {"content-length", "10"},
                                                               {"etag", "a1"}};

  // Send first request, and get response from upstream.
  {
    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(original_response_headers, /*end_stream=*/false);
    // send 10 'a's
    upstream_request_->encodeData(10, true);
    // Wait for the response to be read by the codec client.
    response_decoder->waitForEndStream();
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(original_response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::Headers::get().Age), nullptr);
    EXPECT_EQ(response_decoder->body(), std::string(10, 'a'));
    EXPECT_EQ(waitForAccessLog(access_log_name_), "- via_upstream\n");
  }

  simTime().advanceTimeWait(std::chrono::seconds(10));
  // Any response with status other than 304 should be passed to the client as-is.
  Http::TestResponseHeaderMapImpl updated_response_headers = {{":status", "200"},
                                                              {"date", formatter_.now(simTime())},
                                                              {"cache-control", "max-age=0"},
                                                              {"content-length", "20"},
                                                              {"etag", "a2"}};

  // Send second request, validation of the cached response should be attempted but should fail.
  IntegrationStreamDecoderPtr response_decoder =
      codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Check for injected precondition headers.
  Http::TestRequestHeaderMapImpl injected_headers = {{"if-none-match", "a1"}};
  EXPECT_THAT(upstream_request_->headers(), IsSupersetOfHeaders(injected_headers));

  // Reply with the updated response -> cached response is invalid.
  upstream_request_->encodeHeaders(updated_response_headers, /*end_stream=*/false);
  // send 20 'a's
  upstream_request_->encodeData(20, true);

  // Wait for the response to be read by the codec client.
  response_decoder->waitForEndStream();
  // Check that the served response is the updated response.
  EXPECT_TRUE(response_decoder->complete());
  EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(updated_response_headers));
  EXPECT_EQ(response_decoder->body(), std::string(20, 'a'));
  // Check that age header does not exist as this is not a cached response.
  EXPECT_EQ(response_decoder->headers().get(Http::Headers::get().Age), nullptr);

  // Advance time to force a log flush.
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_EQ(waitForAccessLog(access_log_name_, 1), "- via_upstream\n");
}

// Send the same GET request with body and trailers twice, then check that the response
// doesn't have an age header, to confirm that it wasn't served from cache.
TEST_P(CacheIntegrationTest, GetRequestWithBodyAndTrailers) {
  // Set system time to cause Envoy's cached formatted time to match time on this thread.
  simTime().setSystemTime(std::chrono::hours(1));
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
      {":scheme", "http"},
      {":authority", "MissInsertHit"}};
  Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                   {"request2", "trailer2"}};
  Http::TestResponseHeaderMapImpl response_headers = {{":status", "200"},
                                                      {"date", formatter_.now(simTime())},
                                                      {"cache-control", "public,max-age=3600"},
                                                      {"content-length", "42"}};

  for (int i = 0; i < 2; ++i) {
    auto encoder_decoder = codec_client_->startRequest(request_headers);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, 13, false);
    codec_client_->sendTrailers(*request_encoder_, request_trailers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 42 'a's
    upstream_request_->encodeData(42, true);
    // Wait for the response to be read by the codec client.
    response->waitForEndStream();
    EXPECT_TRUE(response->complete());
    EXPECT_THAT(response->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response->headers().get(Http::Headers::get().Age), nullptr);
    EXPECT_EQ(response->body(), std::string(42, 'a'));
  }
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
