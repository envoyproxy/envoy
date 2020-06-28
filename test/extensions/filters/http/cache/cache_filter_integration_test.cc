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
    EXPECT_EQ(waitForAccessLog(access_log_name_), "- via_upstream\n");
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
  EXPECT_EQ(waitForAccessLog(access_log_name_, 1), "RFCF cache.response_from_cache_filter\n");
}

// Send the same GET request twice with body and trailers twice, then check that the response
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
