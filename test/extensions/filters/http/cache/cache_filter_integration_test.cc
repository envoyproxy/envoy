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

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;
using Http::HeaderValueOf;
using testing::_;
using testing::AllOf;
using testing::Eq;
using testing::HasSubstr;
using testing::Not;
using testing::Pointee;
using testing::Property;

// TODO(toddmgreer): Expand integration test to include age header values,
// expiration, HEAD requests, config customizations,
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

  static HttpFilterProto codecFilterConfig() {
    HttpFilterProto filter_config;
    filter_config.set_name("envoy.filters.http.upstream_codec");
    auto configuration = envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec();
    filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  void addFilter(const std::string& config) {
    HttpFilterProto config_proto;
    TestUtility::loadFromYaml(config, config_proto);
    config_helper_.addConfigModifier(
        [config_proto](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          HttpFilterProto& router_filter = *hcm.mutable_http_filters()->rbegin();
          ASSERT_EQ(router_filter.name(), "envoy.filters.http.router");
          envoy::extensions::filters::http::router::v3::Router router;
          router_filter.typed_config().UnpackTo(&router);
          *router.add_upstream_http_filters() = config_proto;
          *router.add_upstream_http_filters() = codecFilterConfig();
          router_filter.mutable_typed_config()->PackFrom(router);
        });
  }

  void initializeFilter(const std::string& config) {
    addFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  void initializeFilterWithTrailersEnabled(const std::string& config) {
    addFilter(config);
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

  IntegrationStreamDecoderPtr sendHeaderOnlyRequest(const Http::TestRequestHeaderMapImpl& headers) {
    IntegrationStreamDecoderPtr response_decoder = codec_client_->makeHeaderOnlyRequest(headers);
    return response_decoder;
  }

  void awaitResponse(IntegrationStreamDecoderPtr& response_decoder) {
    EXPECT_TRUE(response_decoder->waitForEndStream());
    EXPECT_TRUE(response_decoder->complete());
  }

  IntegrationStreamDecoderPtr sendHeaderOnlyRequestAwaitResponse(
      const Http::TestRequestHeaderMapImpl& headers,
      std::function<void()> simulate_upstream = []() {}) {
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequest(headers);
    simulate_upstream();
    // Wait for the response to be read by the codec client.
    awaitResponse(response_decoder);
    return response_decoder;
  }

  // split_body allows us to test the behavior when encodeData is in more than one part.
  std::function<void()> simulateUpstreamResponse(
      const Http::TestResponseHeaderMapImpl& headers, OptRef<const std::string> body,
      OptRef<const Http::TestResponseTrailerMapImpl> trailers, bool split_body = false) {
    return [this, &headers, body = std::move(body), trailers = std::move(trailers),
            split_body]() mutable {
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(headers, /*end_stream=*/!body && !trailers.has_value());
      if (body.has_value()) {
        if (split_body) {
          upstream_request_->encodeData(body.ref().substr(0, body.ref().size() / 2), false);
          upstream_request_->encodeData(body.ref().substr(body.ref().size() / 2),
                                        !trailers.has_value());
        } else {
          upstream_request_->encodeData(body.ref(), !trailers.has_value());
        }
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
  OptRef<const std::string> no_body_;
  OptRef<const Http::TestResponseTrailerMapImpl> no_trailers_;
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, CacheIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CacheIntegrationTest, MissInsertHit) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"MissInsertHit");
  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);

  // Send first request, and get response from upstream.
  // use split_body to cover multipart body responses.
  {
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_, true));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }

  // Advance time, to verify the original date header is preserved.
  simTime().advanceTimeWait(Seconds(10));

  // Send second request, and get response from cache.
  {
    IntegrationStreamDecoderPtr response_decoder =
        sendHeaderOnlyRequestAwaitResponse(request_headers, serveFromCache());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(response_decoder->headers(),
                HeaderHasValueRef(Http::CustomHeaders::get().Age, "10"));
    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
                HasSubstr("RFCF cache.response_from_cache_filter"));
  }
}

TEST_P(CacheIntegrationTest, ParallelRequestsShareInsert) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"ParallelRequestsShareInsert");
  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);
  // Send three requests.
  auto codec_client_2 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto codec_client_3 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  IntegrationStreamDecoderPtr response_decoder1 =
      codec_client_->makeHeaderOnlyRequest(request_headers);
  IntegrationStreamDecoderPtr response_decoder2 =
      codec_client_2->makeHeaderOnlyRequest(request_headers);
  IntegrationStreamDecoderPtr response_decoder3 =
      codec_client_3->makeHeaderOnlyRequest(request_headers);
  // Use split_body to cover multipart body responses.
  simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_, true)();
  awaitResponse(response_decoder1);
  awaitResponse(response_decoder2);
  awaitResponse(response_decoder3);
  EXPECT_THAT(response_decoder1->headers(), IsSupersetOfHeaders(response_headers));
  EXPECT_THAT(response_decoder2->headers(), IsSupersetOfHeaders(response_headers));
  EXPECT_THAT(response_decoder3->headers(), IsSupersetOfHeaders(response_headers));
  // Two of the responses should have an age, and one should not.
  // Which of the requests get the age header depends on the order of
  // parallel request resolution, which is not relevant to this test.
  EXPECT_THAT(response_decoder1->headers().get(Http::CustomHeaders::get().Age).size() +
                  response_decoder2->headers().get(Http::CustomHeaders::get().Age).size() +
                  response_decoder3->headers().get(Http::CustomHeaders::get().Age).size(),
              Eq(2));
  EXPECT_EQ(response_decoder1->body(), response_body);
  EXPECT_EQ(response_decoder2->body(), response_body);
  EXPECT_EQ(response_decoder3->body(), response_body);
  codec_client_2->close();
  codec_client_3->close();
  // Advance time to force a log flush.
  simTime().advanceTimeWait(Seconds(1));

  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true),
              HasSubstr("RFCF cache.insert_via_upstream"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1, true),
              HasSubstr("RFCF cache.response_from_cache_filter"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2, true),
              HasSubstr("RFCF cache.response_from_cache_filter"));
}

TEST_P(CacheIntegrationTest, ParallelRangeRequestsShareInsertAndGetDistinctResponses) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"ParallelRequestsShareInsert");
  Http::TestRequestHeaderMapImpl request_headers_2 = request_headers;
  Http::TestRequestHeaderMapImpl request_headers_3 = request_headers;
  request_headers.setReference(Http::Headers::get().Range, "bytes=0-4");
  request_headers_2.setReference(Http::Headers::get().Range, "bytes=5-9");
  request_headers_3.setReference(Http::Headers::get().Range, "bytes=3-6");
  const std::string response_body("helloworld");
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);
  // Send three requests.
  auto codec_client_2 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto codec_client_3 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  IntegrationStreamDecoderPtr response_decoder1 =
      codec_client_->makeHeaderOnlyRequest(request_headers);
  IntegrationStreamDecoderPtr response_decoder2 =
      codec_client_2->makeHeaderOnlyRequest(request_headers_2);
  IntegrationStreamDecoderPtr response_decoder3 =
      codec_client_3->makeHeaderOnlyRequest(request_headers_3);
  // Use split_body to cover multipart body responses.
  simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_, true)();
  awaitResponse(response_decoder1);
  awaitResponse(response_decoder2);
  awaitResponse(response_decoder3);
  EXPECT_THAT(response_decoder1->headers(),
              AllOf(HeaderValueOf("content-range", "bytes 0-4/10"),
                    HeaderValueOf("content-length", "5"), HeaderValueOf(":status", "206")));
  EXPECT_THAT(response_decoder2->headers(),
              AllOf(HeaderValueOf("content-range", "bytes 5-9/10"),
                    HeaderValueOf("content-length", "5"), HeaderValueOf(":status", "206")));
  EXPECT_THAT(response_decoder3->headers(),
              AllOf(HeaderValueOf("content-range", "bytes 3-6/10"),
                    HeaderValueOf("content-length", "4"), HeaderValueOf(":status", "206")));
  // Two of the responses should have an age, and one should not.
  // Which of the requests get the age header depends on the order of
  // parallel request resolution, which is not relevant to this test.
  EXPECT_THAT(response_decoder1->headers().get(Http::CustomHeaders::get().Age).size() +
                  response_decoder2->headers().get(Http::CustomHeaders::get().Age).size() +
                  response_decoder3->headers().get(Http::CustomHeaders::get().Age).size(),
              Eq(2));
  EXPECT_EQ(response_decoder1->body(), "hello");
  EXPECT_EQ(response_decoder2->body(), "world");
  EXPECT_EQ(response_decoder3->body(), "lowo");
  codec_client_2->close();
  codec_client_3->close();
  // Advance time to force a log flush.
  simTime().advanceTimeWait(Seconds(1));

  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true),
              HasSubstr("RFCF cache.insert_via_upstream"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1, true),
              HasSubstr("RFCF cache.response_from_cache_filter"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2, true),
              HasSubstr("RFCF cache.response_from_cache_filter"));
}

TEST_P(CacheIntegrationTest, RequestNoCacheProvokesValidationAndOnFailureInsert) {
  initializeFilter(default_config);
  Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"RequestNoCacheProvokesValidationAndOnFailureInsert");
  request_headers.setReference(Http::CustomHeaders::get().CacheControl, "no-cache");
  const std::string response_body("helloworld");
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);
  // send two requests in parallel, they should share a response because
  // validation is implicit if it's cacheable and same-time.
  auto codec_client_2 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  IntegrationStreamDecoderPtr response_decoder1 =
      codec_client_->makeHeaderOnlyRequest(request_headers);
  IntegrationStreamDecoderPtr response_decoder2 =
      codec_client_2->makeHeaderOnlyRequest(request_headers);
  simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_, true)();
  EXPECT_THAT(upstream_request_->headers(), AllOf(HeaderValueOf("cache-control", "no-cache"),
                                                  Not(HeaderValueOf("if-modified-since", _))));
  awaitResponse(response_decoder1);
  awaitResponse(response_decoder2);
  EXPECT_EQ(response_decoder1->body(), "helloworld");
  EXPECT_EQ(response_decoder2->body(), "helloworld");
  codec_client_2->close();
  // send a request subsequent to cache being populated, which should validate
  auto codec_client_3 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  IntegrationStreamDecoderPtr response_decoder3 =
      codec_client_3->makeHeaderOnlyRequest(request_headers);
  // Response with a 200 status, implying validation failed.
  simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_, true)();
  // Additional upstream request should be a validation, so should have if-modified-since
  EXPECT_THAT(upstream_request_->headers(), AllOf(HeaderValueOf("cache-control", "no-cache"),
                                                  HeaderValueOf("if-modified-since", _)));
  awaitResponse(response_decoder3);
  EXPECT_EQ(response_decoder3->body(), "helloworld");
  codec_client_3->close();
  // Advance time to force a log flush.
  simTime().advanceTimeWait(Seconds(1));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true),
              HasSubstr("RFCF cache.insert_via_upstream"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1, true),
              HasSubstr("RFCF cache.response_from_cache_filter"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2, true),
              HasSubstr("RFCF cache.insert_via_upstream"));
}

TEST_P(CacheIntegrationTest, RequestNoCacheProvokesValidationAndOnSuccessReadsFromCache) {
  initializeFilter(default_config);
  Http::TestRequestHeaderMapImpl request_headers = httpRequestHeader(
      "GET", /*authority=*/"RequestNoCacheProvokesValidationAndOnSuccessReadsFromCache");
  request_headers.setReference(Http::CustomHeaders::get().CacheControl, "no-cache");
  const std::string response_body("helloworld");
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);
  // send two requests in parallel, they should share a response because
  // validation is implicit if it's cacheable and same-time.
  auto codec_client_2 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  IntegrationStreamDecoderPtr response_decoder1 =
      codec_client_->makeHeaderOnlyRequest(request_headers);
  IntegrationStreamDecoderPtr response_decoder2 =
      codec_client_2->makeHeaderOnlyRequest(request_headers);
  simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_, true)();
  EXPECT_THAT(upstream_request_->headers(), AllOf(HeaderValueOf("cache-control", "no-cache"),
                                                  Not(HeaderValueOf("if-modified-since", _))));
  awaitResponse(response_decoder1);
  awaitResponse(response_decoder2);
  EXPECT_EQ(response_decoder1->body(), "helloworld");
  EXPECT_EQ(response_decoder2->body(), "helloworld");
  codec_client_2->close();
  // send a request subsequent to cache being populated, which should validate
  auto codec_client_3 = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  IntegrationStreamDecoderPtr response_decoder3 =
      codec_client_3->makeHeaderOnlyRequest(request_headers);
  // Response with a 304 status, implying validation succeeded.
  Http::TestResponseHeaderMapImpl response_headers_304{
      {":status", "304"}, {"last-modified", "Mon, 01 Jan 1970 00:30:00 GMT"}};
  simulateUpstreamResponse(response_headers_304, absl::nullopt, no_trailers_, true)();
  // Additional upstream request should be a validation, so should have if-modified-since
  EXPECT_THAT(upstream_request_->headers(), AllOf(HeaderValueOf("cache-control", "no-cache"),
                                                  HeaderValueOf("if-modified-since", _)));
  awaitResponse(response_decoder3);
  EXPECT_EQ(response_decoder3->body(), "helloworld");
  codec_client_3->close();
  // Advance time to force a log flush.
  simTime().advanceTimeWait(Seconds(1));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true),
              HasSubstr("RFCF cache.insert_via_upstream"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1, true),
              HasSubstr("RFCF cache.response_from_cache_filter"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2, true),
              HasSubstr("RFCF cache.response_from_cache_filter"));
}

TEST_P(CacheIntegrationTest, ExpiredValidated) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"ExpiredValidated");
  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(
      response_body, /*cache_control=*/"max-age=10", /*extra_headers=*/{{"etag", "abc123"}});

  // Send first request, and get response from upstream.
  {
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0)
        << "age="
        << response_decoder->headers()
               .get(Http::CustomHeaders::get().Age)[0]
               ->value()
               .getStringView();
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }

  // Advance time for the cached response to be stale (expired)
  // Also to make sure response date header gets updated with the 304 date
  simTime().advanceTimeWait(Seconds(11));

  // Send second request, the cached response should be validate then served
  {
    // Create a 304 (not modified) response -> cached response is valid
    const std::string not_modified_date = formatter_.now(simTime());
    const Http::TestResponseHeaderMapImpl not_modified_response_headers = {
        {":status", "304"}, {"date", not_modified_date}};

    IntegrationStreamDecoderPtr response_decoder =
        sendHeaderOnlyRequestAwaitResponse(request_headers, [&]() {
          waitForNextUpstreamRequest();
          // Check for injected precondition headers
          EXPECT_THAT(upstream_request_->headers(), HeaderValueOf("if-none-match", "abc123"));

          upstream_request_->encodeHeaders(not_modified_response_headers, /*end_stream=*/true);
        });

    // The original response headers should be updated with 304 response headers
    response_headers.setDate(not_modified_date);

    // Check that the served response is the cached response
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body(), response_body);

    // A response that has been validated should not contain an Age header as it is equivalent to
    // a freshly served response from the origin, unless the 304 response has an Age header, which
    // means it was served by an upstream cache.
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0)
        << "age="
        << response_decoder->headers()
               .get(Http::CustomHeaders::get().Age)[0]
               ->value()
               .getStringView();
    ;
  }
  // Advance time to get a fresh cached response
  simTime().advanceTimeWait(Seconds(1));

  // Send third request. The cached response was validated, thus it should have an Age header like
  // fresh responses
  {
    IntegrationStreamDecoderPtr response_decoder =
        sendHeaderOnlyRequestAwaitResponse(request_headers, serveFromCache());
    EXPECT_THAT(response_decoder->headers(),
                HeaderHasValueRef(Http::CustomHeaders::get().Age, "1"));

    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
                HasSubstr("RFCF cache.response_from_cache_filter"));
  }
}

TEST_P(CacheIntegrationTest, TemporarilyUncacheableEventuallyCaches) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"TemporarilyUncacheableEventuallyCaches");
  std::string response_body{"aaaaaaaaaa"};
  Http::TestResponseHeaderMapImpl cacheable_response_headers = httpResponseHeadersForBody(
      response_body, /*cache_control=*/"max-age=10", /*extra_headers=*/{{"etag", "abc123"}});

  // Send first request, and get 500 response from upstream.
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers, simulateUpstreamResponse(response_headers, absl::nullopt, no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }
  // Send second request, and get cacheable 200 response from upstream.
  // This should reset the uncacheable state imposed by the first request.
  // *Ideally* this would write to the cache this time as well, but getting
  // to this state means we already started an inexpensive pass-through, so
  // it's too late to start writing to the cache from this request without
  // adding unnecessary complexity.
  {
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(cacheable_response_headers, response_body, no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(cacheable_response_headers));
    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("via_upstream"));
  }
  // Send third request, and get cacheable 200 response from upstream, it should be cached this
  // time.
  {
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(cacheable_response_headers, response_body, no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(cacheable_response_headers));
    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 2), HasSubstr("cache.insert_via_upstream"));
  }
}

TEST_P(CacheIntegrationTest, ExpiredFetchedNewResponse) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"ExpiredFetchedNewResponse");

  // Send first request, and get response from upstream.
  {
    const std::string response_body(10, 'a');
    Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(
        response_body, /*cache_control=*/"max-age=10", /*extra_headers=*/{{"etag", "a1"}});
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }

  // Advance time for the cached response to be stale (expired)
  // Also to make sure response date header gets updated with the 304 date
  simTime().advanceTimeWait(Seconds(11));

  // Send second request, validation of the cached response should be attempted but should fail
  // The new response should be served
  {
    const std::string response_body(20, 'a');
    Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(
        response_body,
        /*cache_control=*/"max-age=10", /*extra_headers=*/{{"etag", "a2"}});

    IntegrationStreamDecoderPtr response_decoder =
        sendHeaderOnlyRequestAwaitResponse(request_headers, [&]() {
          waitForNextUpstreamRequest();
          // Check for injected precondition headers
          EXPECT_THAT(upstream_request_->headers(), HeaderValueOf("if-none-match", "a1"));

          // Reply with the updated response -> cached response is invalid
          upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
          // send 20 'a's
          upstream_request_->encodeData(response_body, /*end_stream=*/true);
        });
    // Check that the served response is the updated response
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body(), response_body);
    // Check that age header does not exist as this is not a cached response
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);

    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("via_upstream"));
  }
}

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

TEST_P(CacheIntegrationTest, GetRequestWithResponseTrailers) {
  initializeFilterWithTrailersEnabled(default_config);
  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader("GET", /*authority=*/"GetRequestWithResponseTrailers");

  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = {{":status", "200"},
                                                      {"date", formatter_.now(simTime())},
                                                      {"cache-control", "public,max-age=3600"}};
  const Http::TestResponseTrailerMapImpl response_trailers{{"response1", "trailer1"},
                                                           {"response2", "trailer2"}};
  // Send GET request, receive a response from upstream, cache it
  {
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers, simulateUpstreamResponse(response_headers, makeOptRef(response_body),
                                                  makeOptRef(response_trailers)));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(response_decoder->trailers(), Pointee(IsSupersetOfHeaders(response_trailers)));
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }

  // Advance time, to verify the original date header is preserved.
  simTime().advanceTimeWait(Seconds(10));
  // Send second request, and get response from cache.
  {
    IntegrationStreamDecoderPtr response_decoder =
        sendHeaderOnlyRequestAwaitResponse(request_headers, serveFromCache());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_THAT(response_decoder->headers(),
                HeaderHasValueRef(Http::CustomHeaders::get().Age, "10"));
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(response_decoder->trailers(), Pointee(IsSupersetOfHeaders(response_trailers)));
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
                HasSubstr("RFCF cache.response_from_cache_filter"));
  }
}

TEST_P(CacheIntegrationTest, ServeHeadRequest) {
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestRequestHeaderMapImpl request_headers =
      httpRequestHeader(Http::Headers::get().MethodValues.Head, "ServeHeadRequest");
  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);

  // Send first request, and get response from upstream.
  {
    // Since it is a head request, no need to encodeData => the response_body is absl::nullopt.
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers, simulateUpstreamResponse(response_headers, no_body_, no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    EXPECT_EQ(response_decoder->body().size(), 0);
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }

  // Advance time, to verify the original date header is preserved.
  simTime().advanceTimeWait(Seconds(10));

  // Send second request, and get response from upstream, since the head requests are not stored
  // in cache.
  {
    // Since it is a head request, no need to encodeData => the response_body is empty.
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers, simulateUpstreamResponse(response_headers, no_body_, no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body().size(), 0);
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("via_upstream"));
  }
}

TEST_P(CacheIntegrationTest, ServeHeadFromCacheAfterGetRequest) {
  initializeFilter(default_config);

  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);

  // Send GET request, and get response from upstream.
  {
    // Include test name and params in URL to make each test's requests unique.
    const Http::TestRequestHeaderMapImpl request_headers =
        httpRequestHeader("GET", /*authority=*/"ServeHeadFromCacheAfterGetRequest");
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }
  // Advance time, to verify the original date header is preserved.
  simTime().advanceTimeWait(Seconds(10));

  // Send HEAD request, and get response from cache.
  {
    // Include test name and params in URL to make each test's requests unique.
    const Http::TestRequestHeaderMapImpl request_headers =
        httpRequestHeader("HEAD", "ServeHeadFromCacheAfterGetRequest");
    IntegrationStreamDecoderPtr response_decoder =
        sendHeaderOnlyRequestAwaitResponse(request_headers, serveFromCache());
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body().size(), 0);
    EXPECT_THAT(response_decoder->headers(),
                HeaderHasValueRef(Http::CustomHeaders::get().Age, "10"));
    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
                HasSubstr("RFCF cache.response_from_cache_filter"));
  }
}

TEST_P(CacheIntegrationTest, ServeGetFromUpstreamAfterHeadRequest) {
  initializeFilter(default_config);

  const std::string response_body(42, 'a');
  Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(response_body);

  // Send HEAD request, and get response from upstream.
  {
    // Include test name and params in URL to make each test's requests unique.
    const Http::TestRequestHeaderMapImpl request_headers =
        httpRequestHeader("HEAD", "ServeGetFromUpstreamAfterHeadRequest");
    // No need to encode the data, therefore response_body is empty.
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers, simulateUpstreamResponse(response_headers, no_body_, no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    EXPECT_EQ(response_decoder->body().size(), 0);
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }

  // Send GET request, and get response from upstream.
  {
    // Include test name and params in URL to make each test's requests unique.
    const Http::TestRequestHeaderMapImpl request_headers =
        httpRequestHeader("GET", /*authority=*/"ServeGetFromUpstreamAfterHeadRequest");
    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);

    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));

    EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("via_upstream"));
  }
}

TEST_P(CacheIntegrationTest, ServeGetFollowedByHead200ThatNeedsValidationPassesThroughHeadRequest) {
  initializeFilter(default_config);

  // Send GET request, and get response from upstream.
  {
    // Include test name and params in URL to make each test's requests unique.
    const Http::TestRequestHeaderMapImpl request_headers =
        httpRequestHeader("GET", /*authority=*/"ServeGetFollowedByHead200WithValidation");
    const std::string response_body(10, 'a');
    Http::TestResponseHeaderMapImpl response_headers = httpResponseHeadersForBody(
        response_body, /*cache-control*/ "max-age=10", /*extra_headers=*/{{"etag", "a1"}});

    IntegrationStreamDecoderPtr response_decoder = sendHeaderOnlyRequestAwaitResponse(
        request_headers,
        simulateUpstreamResponse(response_headers, makeOptRef(response_body), no_trailers_));
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);
    EXPECT_EQ(response_decoder->body(), response_body);
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
  }

  // Advance time for the cached response to be stale (expired)
  // Also to make sure response date header gets updated with the 304 date
  simTime().advanceTimeWait(Seconds(11));

  // Send HEAD request, validation of the cached response should be attempted but should fail
  {
    // Include test name and params in URL to make each test's requests unique.
    const Http::TestRequestHeaderMapImpl request_headers =
        httpRequestHeader("HEAD", "ServeGetFollowedByHead200WithValidation");
    const std::string response_body(20, 'a');
    Http::TestResponseHeaderMapImpl response_headers =
        httpResponseHeadersForBody(response_body,
                                   /*cache_control=*/"max-age=10",
                                   /*extra_headers=*/{{"etag", "a2"}});

    IntegrationStreamDecoderPtr response_decoder =
        sendHeaderOnlyRequestAwaitResponse(request_headers, [&]() {
          waitForNextUpstreamRequest();

          // Reply with the updated response -> cached response is invalid
          upstream_request_->encodeHeaders(response_headers,
                                           /*end_stream=*/true);
        });
    EXPECT_THAT(response_decoder->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(response_decoder->body().size(), 0);
    // Check that age header does not exist as this is not a cached response
    EXPECT_EQ(response_decoder->headers().get(Http::CustomHeaders::get().Age).size(), 0);

    // Advance time to force a log flush.
    simTime().advanceTimeWait(Seconds(1));
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("via_upstream"));
  }
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
