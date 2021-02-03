#include "test/integration/http_protocol_integration.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

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

  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = {
      {":status", "200"},
      {"date", formatter_.now(simTime())},
      {"cache-control", "public,max-age=3600"},
      {"content-length", std::to_string(response_body.size())}};

  // Send first request, and get response from upstream.
  {
    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 42 'a's
    upstream_request_->encodeData(response_body, /*end_stream=*/true);
    // Wait for the response to be read by the codec client.
    response_decoder->waitForEndStream();
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_TRUE(response_decoder->headers().get(Http::Headers::get().Age).empty());
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("- via_upstream"));
  }

  // Advance time, to verify the original date header is preserved.
  simTime().advanceTimeWait(Seconds(10));

  // Send second request, and get response from cache.
  {
    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    response_decoder->waitForEndStream();
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(response_decoder->headers(), HeaderHasValueRef(Http::Headers::get().Age, "10"));
    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
                testing::HasSubstr("RFCF cache.response_from_cache_filter"));
  }
}

TEST_P(CacheIntegrationTest, ExpiredValidated) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  // Set system time to cause Envoy's cached formatted time to match time on this thread.
  simTime().setSystemTime(std::chrono::hours(1));
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
      {":scheme", "http"},
      {":authority", "ExpiredValidated"}};

  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = {
      {":status", "200"},
      {"date", formatter_.now(simTime())},
      {"cache-control", "max-age=10"}, // expires after 10 s
      {"content-length", std::to_string(response_body.size())},
      {"etag", "abc123"}};

  // Send first request, and get response from upstream.
  {
    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 42 'a's
    upstream_request_->encodeData(response_body, true);
    // Wait for the response to be read by the codec client.
    response_decoder->waitForEndStream();
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_TRUE(response_decoder->headers().get(Http::Headers::get().Age).empty());
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("- via_upstream"));
  }

  // Advance time for the cached response to be stale (expired)
  // Also to make sure response date header gets updated with the 304 date
  simTime().advanceTimeWait(Seconds(11));

  // Send second request, the cached response should be validate then served
  {
    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();

    // Check for injected precondition headers
    const Http::TestRequestHeaderMapImpl injected_headers = {{"if-none-match", "abc123"}};
    EXPECT_THAT(upstream_request_->headers(), IsSupersetOfHeaders(injected_headers));

    // Create a 304 (not modified) response -> cached response is valid
    const std::string not_modified_date = formatter_.now(simTime());
    const Http::TestResponseHeaderMapImpl not_modified_response_headers = {
        {":status", "304"}, {"date", not_modified_date}};
    upstream_request_->encodeHeaders(not_modified_response_headers, /*end_stream=*/true);

    // The original response headers should be updated with 304 response headers
    response_headers.setDate(not_modified_date);

    // Wait for the response to be read by the codec client.
    response_decoder->waitForEndStream();

    // Check that the served response is the cached response
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body(), response_body);

    // A response that has been validated should not contain an Age header as it is equivalent to a
    // freshly served response from the origin, unless the 304 response has an Age header, which
    // means it was served by an upstream cache.
    EXPECT_TRUE(response_decoder->headers().get(Http::Headers::get().Age).empty());

    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
                testing::HasSubstr("RFCF cache.response_from_cache_filter"));
  }
}

TEST_P(CacheIntegrationTest, ExpiredFetchedNewResponse) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  // Set system time to cause Envoy's cached formatted time to match time on this thread.
  simTime().setSystemTime(std::chrono::hours(1));
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
      {":scheme", "http"},
      {":authority", "ExpiredFetchedNewResponse"}};

  // Send first request, and get response from upstream.
  {
    const std::string response_body(10, 'a');
    Http::TestResponseHeaderMapImpl response_headers = {
        {":status", "200"},
        {"date", formatter_.now(simTime())},
        {"cache-control", "max-age=10"}, // expires after 10 s
        {"content-length", std::to_string(response_body.size())},
        {"etag", "a1"}};

    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 10 'a's
    upstream_request_->encodeData(response_body, /*end_stream=*/true);
    // Wait for the response to be read by the codec client.
    response_decoder->waitForEndStream();
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_TRUE(response_decoder->headers().get(Http::Headers::get().Age).empty());
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("- via_upstream"));
  }

  // Advance time for the cached response to be stale (expired)
  // Also to make sure response date header gets updated with the 304 date
  simTime().advanceTimeWait(Seconds(11));

  // Send second request, validation of the cached response should be attempted but should fail
  // The new response should be served
  {
    const std::string response_body(20, 'a');
    Http::TestResponseHeaderMapImpl response_headers = {
        {":status", "200"},
        {"date", formatter_.now(simTime())},
        {"content-length", std::to_string(response_body.size())},
        {"etag", "a2"}};

    IntegrationStreamDecoderPtr response_decoder =
        codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();

    // Check for injected precondition headers
    Http::TestRequestHeaderMapImpl injected_headers = {{"if-none-match", "a1"}};
    EXPECT_THAT(upstream_request_->headers(), IsSupersetOfHeaders(injected_headers));

    // Reply with the updated response -> cached response is invalid
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 20 'a's
    upstream_request_->encodeData(response_body, /*end_stream=*/true);

    // Wait for the response to be read by the codec client.
    response_decoder->waitForEndStream();
    // Check that the served response is the updated response
    EXPECT_TRUE(response_decoder->complete());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body(), response_body);
    // Check that age header does not exist as this is not a cached response
    EXPECT_TRUE(response_decoder->headers().get(Http::Headers::get().Age).empty());

    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("- via_upstream"));
  }
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
    EXPECT_TRUE(response->headers().get(Http::Headers::get().Age).empty());
    EXPECT_EQ(response->body(), std::string(42, 'a'));
  }
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
