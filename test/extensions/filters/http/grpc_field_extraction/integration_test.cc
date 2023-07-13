#include <initializer_list>
#include <optional>

#include "envoy/common/optref.h"

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
  void SetUp() override {
    useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
    // Set system time to cause Envoy's cached formatted time to match time on this thread.
    simTime().setSystemTime(std::chrono::hours(1));
  }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  void initializeFilter(const std::string& config) {
    config_helper_.prependFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  void initializeFilterWithTrailersEnabled(const std::string& config) {
    config_helper_.addFilter(config);
    config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
    config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  Http::TestRequestHeaderMapImpl httpRequestHeader(std::string method, std::string authority) {
    return {{":method", method},
            {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
            {":scheme", "http"},
            {":authority", authority}};
  }

  Http::TestResponseHeaderMapImpl httpResponseHeadersForBody(
      const std::string& body, const std::string& cache_control = "public,max-age=3600",
      std::initializer_list<std::pair<std::string, std::string>> extra_headers = {}) {
    Http::TestResponseHeaderMapImpl response = {{":status", "200"},
                                                {"date", formatter_.now(simTime())},
                                                {"cache-control", cache_control},
                                                {"content-length", std::to_string(body.size())}};
    for (auto& header : extra_headers) {
      response.addCopy(header.first, header.second);
    }
    return response;
  }

  IntegrationStreamDecoderPtr sendHeaderOnlyRequestAwaitResponse(
      const Http::TestRequestHeaderMapImpl& headers,
      std::function<void()> simulate_upstream = []() {}) {
    IntegrationStreamDecoderPtr response_decoder = codec_client_->makeHeaderOnlyRequest(headers);
    simulate_upstream();
    // Wait for the response to be read by the codec client.
    EXPECT_TRUE(response_decoder->waitForEndStream());
    EXPECT_TRUE(response_decoder->complete());
    return response_decoder;
  }

  std::function<void()>
  simulateUpstreamResponse(const Http::TestResponseHeaderMapImpl& headers,
                           OptRef<const std::string> body,
                           OptRef<const Http::TestResponseTrailerMapImpl> trailers) {
    return [this, headers = std::move(headers), body = std::move(body),
            trailers = std::move(trailers)]() {
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(headers, /*end_stream=*/!body);
      if (body.has_value()) {
        upstream_request_->encodeData(body.ref(), !trailers.has_value());
      }
      if (trailers.has_value()) {
        upstream_request_->encodeTrailers(trailers.ref());
      }
    };
  }
  std::function<void()> serveFromCache() {
    return []() {};
  };

  const std::string default_config{R"EOF(
    name: "envoy.filters.http.cache"
    typed_config:
        "@type": "type.googleapis.com/envoy.extensions.filters.http.cache.v3.CacheConfig"
        typed_config:
           "@type": "type.googleapis.com/envoy.extensions.http.cache.simple_http_cache.v3.SimpleHttpCacheConfig"
    )EOF"};
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  OptRef<const std::string> empty_body_;
  OptRef<const Http::TestResponseTrailerMapImpl> empty_trailers_;
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, CacheIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);


// Send the same GET request with body and trailers twice, then check that the response
// doesn't have an age header, to confirm that it wasn't served from cache.
TEST_P(CacheIntegrationTest, GetRequestWithBodyAndTrailers) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"GetRequestWithBodyAndTrailers");

  Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                   {"request2", "trailer2"}};
  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);

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
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_THAT(response->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_TRUE(response->headers().get(Http::CustomHeaders::get().Age).empty());
    EXPECT_EQ(response->body(), std::string(42, 'a'));
  }
}



} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
