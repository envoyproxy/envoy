#include <string>

#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "test/extensions/filters/http/ext_proc/test_processor.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/synchronization/notification.h"

namespace Envoy {
namespace {

// End-to-end coverage for the AI Protocol Manager filter. The filter offloads
// the request body into an external buffer as it arrives and replays it back
// into the filter chain once the stream ends (see filter.h). These tests drive
// real requests through a configured Envoy and assert that the upstream still
// observes the headers, the complete body (across a range of sizes), and any
// trailers unchanged -- i.e. the offload/replay round-trip is transparent.
//
// The filter is a dual filter: each scenario runs once with the filter in the
// downstream HTTP filter chain and once in the upstream (cluster) filter chain.
class AiProtocolManagerIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void prependFilter(bool downstream = true) {
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
)EOF",
                                 downstream);
  }

  Http::TestRequestHeaderMapImpl requestHeaders() {
    // The authority must match the upstream test cert (*.lyft.com): with HTTP/3
    // upstreams the cluster uses TLS with auto_sni, so :authority becomes the SNI
    // validated against the served certificate.
    return Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "sni.lyft.com"}};
  }

  // Shared scenario bodies; each is driven with the filter in either chain.
  void runHeaderOnly();
  void runHeaderAndBody();
  void runHeaderAndBodyMultipleFrames();
  void runHeaderAndBodyAndTrailers();
  void runHeaderAndTrailersNoBody();
  void runLocalReplyDuringReplay(bool downstream);
};

INSTANTIATE_TEST_SUITE_P(Protocols, AiProtocolManagerIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Headers-only request: the filter must let the headers flow immediately (there
// is no payload to offload), and the round-trip must complete normally.
void AiProtocolManagerIntegrationTest::runHeaderOnly() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());

  waitForNextUpstreamRequest();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  EXPECT_FALSE(upstream_request_->receivedData());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(AiProtocolManagerIntegrationTest, HeaderOnly) {
  prependFilter();
  runHeaderOnly();
}

TEST_P(AiProtocolManagerIntegrationTest, UpstreamHeaderOnly) {
  prependFilter(/*downstream=*/false);
  runHeaderOnly();
}

// Header + body: the offloaded body must be replayed in full to the upstream.
// Parameterized over a range of sizes to exercise empty, sub-chunk, and
// multi-chunk payloads through the offload/replay path.
void AiProtocolManagerIntegrationTest::runHeaderAndBody() {
  initialize();

  for (const uint64_t body_size : {0u, 1u, 16u, 1024u, 64u * 1024u, 1024u * 1024u}) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    const std::string body(body_size, 'a');
    auto response = codec_client_->makeRequestWithBody(requestHeaders(), body);

    waitForNextUpstreamRequest();
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(body_size, upstream_request_->bodyLength());
    EXPECT_EQ(body, upstream_request_->body().toString());

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }
}

TEST_P(AiProtocolManagerIntegrationTest, HeaderAndBody) {
  prependFilter();
  runHeaderAndBody();
}

TEST_P(AiProtocolManagerIntegrationTest, UpstreamHeaderAndBody) {
  prependFilter(/*downstream=*/false);
  runHeaderAndBody();
}

// Header + body sent as several explicit frames before end_stream. Verifies the
// filter reassembles a body delivered across multiple decodeData() calls.
void AiProtocolManagerIntegrationTest::runHeaderAndBodyMultipleFrames() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(requestHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, "123", false);
  codec_client_->sendData(*request_encoder_, "456", false);
  codec_client_->sendData(*request_encoder_, "789", true);

  waitForNextUpstreamRequest();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("123456789", upstream_request_->body().toString());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(AiProtocolManagerIntegrationTest, HeaderAndBodyMultipleFrames) {
  prependFilter();
  runHeaderAndBodyMultipleFrames();
}

TEST_P(AiProtocolManagerIntegrationTest, UpstreamHeaderAndBodyMultipleFrames) {
  prependFilter(/*downstream=*/false);
  runHeaderAndBodyMultipleFrames();
}

// Header + body + trailers: the stream is terminated by trailers rather than an
// end_stream data frame. The filter must replay the buffered body and then
// release the trailers, so the upstream observes both intact.
void AiProtocolManagerIntegrationTest::runHeaderAndBodyAndTrailers() {
  // HTTP/1.1 codecs only parse/emit trailers when explicitly enabled, on both
  // the downstream (to read client trailers) and upstream (to forward them).
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initialize();

  for (const uint64_t body_size : {0u, 16u, 1024u, 64u * 1024u}) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(requestHeaders());
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);

    const std::string body(body_size, 'b');
    if (body_size > 0) {
      codec_client_->sendData(*request_encoder_, body, false);
    }
    Http::TestRequestTrailerMapImpl request_trailers{{"x-request-trailer", "trailer-value"}};
    codec_client_->sendTrailers(*request_encoder_, request_trailers);

    waitForNextUpstreamRequest();
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(body_size, upstream_request_->bodyLength());
    if (body_size > 0) {
      EXPECT_EQ(body, upstream_request_->body().toString());
    }
    ASSERT_NE(upstream_request_->trailers(), nullptr);
    EXPECT_EQ("trailer-value", upstream_request_->trailers()
                                   ->get(Http::LowerCaseString("x-request-trailer"))[0]
                                   ->value()
                                   .getStringView());

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }
}

TEST_P(AiProtocolManagerIntegrationTest, HeaderAndBodyAndTrailers) {
  prependFilter();
  runHeaderAndBodyAndTrailers();
}

TEST_P(AiProtocolManagerIntegrationTest, UpstreamHeaderAndBodyAndTrailers) {
  prependFilter(/*downstream=*/false);
  runHeaderAndBodyAndTrailers();
}

// The stream is torn down *during replay*: a buffer filter placed downstream of
// the AiProtocolManager trips its request-size limit as the offloaded body is
// replayed back into the chain, so Envoy emits a 413 local reply from inside the
// replay injection. That reaches the filter's onDestroy() on-stack, detaching the
// BufferManager mid-replay (the manager is freed later, at the filter's deferred
// destruction -- see its onDestroy()-before-destruction contract). Since the filter
// holds headers and data until replay completes, a downstream filter is the only
// thing that can react to the replayed bytes, which makes this a deterministic
// end-to-end exercise of the offload/replay teardown path: the manager must unwind
// cleanly (guarding on destroyed_) rather than touch its released buffer/bridge.
// The round-trip must fail cleanly with 413 (and, under ASAN, without touching a
// torn-down manager), and Envoy must stay healthy for a subsequent request.
void AiProtocolManagerIntegrationTest::runLocalReplyDuringReplay(bool downstream) {
  // Order matters: AiProtocolManager must run first (it offloads the body), the
  // buffer filter after it (it sees the body only when the manager replays it).
  // prependFilter() inserts at the head of the chosen chain, so add the buffer
  // filter (also a dual filter) first.
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.buffer
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
  max_request_bytes: 1024
)EOF",
                               downstream);
  prependFilter(downstream);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Several replay chunks (> ReadChunkSize, 64KiB), each far over max_request_bytes,
  // so the buffer filter trips the 413 on the first replayed chunk while later
  // chunks are still pending. That forces the manager to detach mid-range with reads
  // outstanding -- the path that must stop rather than read from the released buffer.
  const std::string body(256u * 1024u, 'a');
  auto response = codec_client_->makeRequestWithBody(requestHeaders(), body);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  codec_client_->close();

  if (!downstream) {
    // With the filter chain in the upstream request, the aborted request above already
    // dispatched to the cluster: the connection pool opened an upstream connection even
    // though the 413 fired before the held headers reached the codec, so no stream was
    // ever sent on it. Depending on reset timing Envoy either keeps that pristine
    // connection idle for reuse or closes it, which would leave the follow-up request's
    // waitForNextUpstreamRequest() watching the wrong connection. Drain it from the test
    // side so the follow-up request deterministically arrives on a fresh connection.
    FakeHttpConnectionPtr aborted_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, aborted_connection));
    ASSERT_TRUE(aborted_connection->close());
    ASSERT_TRUE(aborted_connection->waitForDisconnect());
  }

  // The worker survived the mid-replay teardown: a fresh request round-trips. A
  // small body stays under the buffer filter's limit, so it replays and reaches
  // the upstream normally.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto ok_response = codec_client_->makeRequestWithBody(requestHeaders(), "small");
  waitForNextUpstreamRequest();
  EXPECT_EQ("small", upstream_request_->body().toString());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(ok_response->waitForEndStream());
  EXPECT_EQ("200", ok_response->headers().getStatusValue());
}

TEST_P(AiProtocolManagerIntegrationTest, LocalReplyDuringReplayTearsDownCleanly) {
  runLocalReplyDuringReplay(/*downstream=*/true);
}

TEST_P(AiProtocolManagerIntegrationTest, UpstreamLocalReplyDuringReplayTearsDownCleanly) {
  runLocalReplyDuringReplay(/*downstream=*/false);
}

// Trailers immediately after headers, with no body in between.
void AiProtocolManagerIntegrationTest::runHeaderAndTrailersNoBody() {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(requestHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  Http::TestRequestTrailerMapImpl request_trailers{{"x-request-trailer", "trailer-value"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  waitForNextUpstreamRequest();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  ASSERT_NE(upstream_request_->trailers(), nullptr);

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(AiProtocolManagerIntegrationTest, HeaderAndTrailersNoBody) {
  prependFilter();
  runHeaderAndTrailersNoBody();
}

TEST_P(AiProtocolManagerIntegrationTest, UpstreamHeaderAndTrailersNoBody) {
  prependFilter(/*downstream=*/false);
  runHeaderAndTrailersNoBody();
}

// End-to-end coverage for response token-usage extraction: the filter (as a
// downstream or an upstream HTTP filter) observes SSE/JSON responses, extracts
// usage, and publishes it as dynamic metadata readable by the access logger,
// while the response bytes reach the client unchanged.
class AiProtocolManagerResponseIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void initializeWithResponseHandling(bool upstream_filter) {
    useAccessLog("%DYNAMIC_METADATA(envoy.ai.token_usage)%");
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  response_handling: {}
)EOF",
                                 /*downstream=*/!upstream_filter);
    initialize();
  }

  Http::TestRequestHeaderMapImpl requestHeaders() {
    return Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                          {":path", "/v1/chat/completions"},
                                          {":scheme", "http"},
                                          {":authority", "sni.lyft.com"}};
  }

  Http::TestResponseHeaderMapImpl responseHeaders(absl::string_view content_type) {
    return Http::TestResponseHeaderMapImpl{{":status", "200"},
                                           {"content-type", std::string(content_type)}};
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, AiProtocolManagerResponseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// A streamed SSE response (downstream installation): usage from the terminal
// include_usage chunk lands in dynamic metadata; the streamed bytes reach the
// client unchanged.
TEST_P(AiProtocolManagerResponseIntegrationTest, SseTokenUsageInAccessLog) {
  initializeWithResponseHandling(/*upstream_filter=*/false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(requestHeaders(), "{\"stream\":true}");
  waitForNextUpstreamRequest();

  const std::string chunk1 = "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
                             "\"choices\":[{\"delta\":{\"content\":\"hi\"}}],\"usage\":null}\n\n";
  const std::string chunk2 =
      "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\",\"choices\":[],"
      "\"usage\":{\"prompt_tokens\":19,\"completion_tokens\":10,\"total_tokens\":29}}\n\n"
      "data: [DONE]\n\n";
  upstream_request_->encodeHeaders(responseHeaders("text/event-stream"), false);
  upstream_request_->encodeData(chunk1, false);
  upstream_request_->encodeData(chunk2, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // Observe-only: the response body reaches the client byte-for-byte.
  EXPECT_EQ(chunk1 + chunk2, response->body());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr("\"api_format\":\"openai\""));
  EXPECT_THAT(log, testing::HasSubstr("\"input_tokens\":19"));
  EXPECT_THAT(log, testing::HasSubstr("\"output_tokens\":10"));
  EXPECT_THAT(log, testing::HasSubstr("\"total_tokens\":29"));
}

// The filter installed as an upstream (cluster) HTTP filter: metadata written
// from the upstream encoder chain is visible to the downstream access logger,
// and the request body still round-trips through the upstream offload/replay
// path unchanged.
TEST_P(AiProtocolManagerResponseIntegrationTest, UpstreamFilterJsonTokenUsage) {
  initializeWithResponseHandling(/*upstream_filter=*/true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(requestHeaders(), "{\"model\":\"claude\"}");
  waitForNextUpstreamRequest();
  EXPECT_EQ("{\"model\":\"claude\"}", upstream_request_->body().toString());

  const std::string body = "{\"id\":\"msg_1\",\"type\":\"message\",\"model\":\"claude-opus-5\","
                           "\"usage\":{\"input_tokens\":2095,\"output_tokens\":503}}";
  upstream_request_->encodeHeaders(responseHeaders("application/json"), false);
  upstream_request_->encodeData(body, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(body, response->body());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr("\"api_format\":\"anthropic\""));
  EXPECT_THAT(log, testing::HasSubstr("\"input_tokens\":2095"));
  EXPECT_THAT(log, testing::HasSubstr("\"output_tokens\":503"));
  EXPECT_THAT(log, testing::HasSubstr("\"total_tokens\":2598"));
  EXPECT_THAT(log, testing::HasSubstr("\"model\":\"claude-opus-5\""));
}

// Retries with the upstream installation: each attempt runs its own filter
// instance, but only the winning attempt's response streams to completion, so
// the published usage comes from the winner only (the failed attempt is a
// non-2xx and never engages extraction). Mirrors the retry flow of
// StaticRouterAndClusterFiltersIntegrationTest: with upstream filters the
// failed attempt completes and the retry arrives as a new stream.
TEST_P(AiProtocolManagerResponseIntegrationTest, RetryPublishesWinningAttemptOnly) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_num_retries()->set_value(1);
      });
  initializeWithResponseHandling(/*upstream_filter=*/true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(requestHeaders(), "{}");

  // First attempt fails with a completed 5xx; the router retries on a new
  // stream. The 5xx never engages extraction (non-2xx).
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  // Winning attempt returns usage.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  const std::string body = "{\"type\":\"message\",\"model\":\"claude-opus-5\","
                           "\"usage\":{\"input_tokens\":7,\"output_tokens\":8}}";
  upstream_request_->encodeHeaders(responseHeaders("application/json"), false);
  upstream_request_->encodeData(body, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr("\"input_tokens\":7"));
  EXPECT_THAT(log, testing::HasSubstr("\"output_tokens\":8"));
}

// Response-only installation on the cluster: request headers flow to the
// upstream immediately, while the body is still pending -- the shape that
// deadlocks under the full offload (which holds headers until the payload
// completes). Response extraction is unaffected.
TEST_P(AiProtocolManagerResponseIntegrationTest, ResponseOnlyModeDoesNotHoldRequestHeaders) {
  useAccessLog("%DYNAMIC_METADATA(envoy.ai.token_usage)%");
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  request_handling:
    payload_offload_enabled: false
  response_handling: {}
)EOF",
                               /*downstream=*/false);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(requestHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Headers arrive upstream while the request body has not been sent: no hold.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  codec_client_->sendData(*request_encoder_, "{}", true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(responseHeaders("application/json"), false);
  upstream_request_->encodeData("{\"type\":\"message\",\"model\":\"claude-haiku-4-5\","
                                "\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}",
                                true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr("\"total_tokens\":12"));
  EXPECT_THAT(log, testing::HasSubstr("\"extraction_status\":\"complete\""));
}

// Both placements at once (allowed by the API): the upstream instance
// publishes first (upstream encoder filters run before every downstream
// encoder filter) and owns the namespace; the downstream instance skips its
// duplicate publication instead of merging a second observation into the
// same Struct. The downstream instance here has a tiny event cap, so a merge
// would have produced a visibly hybrid record (partial status with the
// upstream's counts).
TEST_P(AiProtocolManagerResponseIntegrationTest, BothPlacementsSinglePublication) {
  useAccessLog("%DYNAMIC_METADATA(envoy.ai.token_usage)%");
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  response_handling: {}
)EOF",
                               /*downstream=*/false);
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  response_handling:
    max_event_size: 64
)EOF",
                               /*downstream=*/true);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(requestHeaders(), "{}");
  waitForNextUpstreamRequest();

  // Over the downstream instance's 64-byte cap; well under the upstream
  // instance's default cap.
  upstream_request_->encodeHeaders(responseHeaders("text/event-stream"), false);
  upstream_request_->encodeData(
      "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\",\"choices\":[],"
      "\"usage\":{\"prompt_tokens\":19,\"completion_tokens\":10,\"total_tokens\":29}}\n\n"
      "data: [DONE]\n\n",
      true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  const std::string log = waitForAccessLog(access_log_name_);
  // The upstream instance's complete record, unmixed with the downstream
  // instance's would-be partial one.
  EXPECT_THAT(log, testing::HasSubstr("\"total_tokens\":29"));
  EXPECT_THAT(log, testing::HasSubstr("\"extraction_status\":\"complete\""));
  EXPECT_THAT(log, testing::HasSubstr("\"model\":\"gpt-4o\""));
}

// Hedged requests: two upstream attempts are in flight concurrently, each
// with its own upstream filter instance writing to the shared downstream
// StreamInfo. Only the attempt whose response the router selects streams to a
// clean end of stream; the losing attempt is reset and never publishes.
// Mirrors HttpTimeoutIntegrationTest's hedged-per-try-timeout flow (simulated
// time, HTTP/2 both directions so both attempts share one upstream
// connection).
class AiProtocolManagerHedgeIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public Event::TestUsingSimulatedTime,
      public HttpIntegrationTest {
public:
  AiProtocolManagerHedgeIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP2);
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AiProtocolManagerHedgeIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AiProtocolManagerHedgeIntegrationTest, HedgedRequestPublishesWinnerOnly) {
  useAccessLog("%DYNAMIC_METADATA(envoy.ai.token_usage)%");
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  response_handling: {}
)EOF",
                               /*downstream=*/false);
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  // The complete request is sent up front: the upstream-chain filter offloads
  // the payload before releasing headers toward the upstream, so a request
  // whose body trails until the upstream responds would never be issued.
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     // Marks the request internal so the x-envoy-*
                                     // timeout/hedge headers below are honored.
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"},
                                     {"x-envoy-hedge-on-per-try-timeout", "true"},
                                     {"x-envoy-upstream-rq-timeout-ms", "5000"},
                                     {"x-envoy-upstream-rq-per-try-timeout-ms", "400"}},
      "{}");

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger the per-try timeout (not the global timeout): the original
  // attempt stays alive and a hedged second attempt is issued after the 25ms
  // retry backoff.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(400));
  timeSystem().advanceTimeWait(std::chrono::milliseconds(26));

  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // The second attempt wins with a streamed usage-carrying response.
  upstream_request2->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  upstream_request2->encodeData("{\"type\":\"message\",\"model\":\"claude-opus-5\","
                                "\"usage\":{\"input_tokens\":7,\"output_tokens\":8}}",
                                true);
  ASSERT_TRUE(response->waitForEndStream());

  // The losing first attempt is reset by the router once the winner is
  // selected: its filter instance is destroyed before a clean end of stream
  // and never publishes.
  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));
  codec_client_->close();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr("\"input_tokens\":7"));
  EXPECT_THAT(log, testing::HasSubstr("\"output_tokens\":8"));
  EXPECT_THAT(log, testing::HasSubstr("\"extraction_status\":\"complete\""));
}

// The documented ext_proc consumer path: usage metadata written on the
// terminal encode callback must be inside the `metadata_context` of the
// external processor's end-of-stream response_body message. Covers both the
// downstream installation (ext_proc listed before this filter so it runs
// *after* it on the encode path) and the upstream installation (all upstream
// encoder filters run before every downstream filter).
class AiProtocolManagerExtProcIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  AiProtocolManagerExtProcIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP2);
    // Populates the cluster's HttpProtocolOptions.upstream_protocol_options,
    // required once upstream http_filters are configured on the cluster.
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    test_processor_.shutdown();
  }

  void initializeWithExtProc(bool upstream_filter, bool trailer_mode = false) {
    // The processor captures the metadata_context of the stream-terminating
    // message -- the end-of-stream response_body message, or the
    // response_trailers message when the response ends in trailers;
    // observability mode means no responses are needed.
    test_processor_.start(
        GetParam(),
        [this](grpc::ServerReaderWriter<envoy::service::ext_proc::v3::ProcessingResponse,
                                        envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
          envoy::service::ext_proc::v3::ProcessingRequest request;
          while (stream->Read(&request)) {
            const bool terminal =
                (request.has_response_body() && request.response_body().end_of_stream()) ||
                request.has_response_trailers();
            if (!terminal || metadata_seen_.HasBeenNotified()) {
              continue;
            }
            const auto& filter_metadata = request.metadata_context().filter_metadata();
            const auto entry = filter_metadata.find("envoy.ai.token_usage");
            const auto& typed_metadata = request.metadata_context().typed_filter_metadata();
            const auto typed_entry = typed_metadata.find("envoy.ai.token_usage");
            if (entry != filter_metadata.end() && typed_entry != typed_metadata.end()) {
              captured_metadata_ = MessageUtil::getJsonStringFromMessageOrError(entry->second);
              ASSERT_TRUE(typed_entry->second.UnpackTo(&captured_typed_usage_));
              metadata_seen_.Notify();
            }
          }
        });

    // AI Protocol Manager first...
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  response_handling: {}
)EOF",
                                 /*downstream=*/!upstream_filter);

    // ...then ext_proc prepended to the head of the downstream chain: encoder
    // filters run in reverse list order, so in the downstream installation the
    // AI Protocol Manager executes before ext_proc on the encode path.
    config_helper_.addConfigModifier([this, trailer_mode](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* processor_cluster = bootstrap.mutable_static_resources()->add_clusters();
      processor_cluster->set_name("ext_proc_server");
      processor_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");
      auto* address = processor_cluster->mutable_load_assignment()
                          ->add_endpoints()
                          ->add_lb_endpoints()
                          ->mutable_endpoint()
                          ->mutable_address()
                          ->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(GetParam()));
      address->set_port_value(test_processor_.port());
      ConfigHelper::setHttp2(*processor_cluster);

      envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor ext_proc;
      ext_proc.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");
      ext_proc.set_observability_mode(true);
      ext_proc.mutable_processing_mode()->set_request_header_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
      ext_proc.mutable_processing_mode()->set_response_header_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
      if (trailer_mode) {
        // The sharper variant of the documented consumer requirement: with
        // body mode NONE (the ext_proc default) the *only* message that can
        // carry the usage metadata for a trailer-ended response is the
        // response_trailers message, which requires SEND (default is SKIP).
        ext_proc.mutable_processing_mode()->set_response_trailer_mode(
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
      } else {
        ext_proc.mutable_processing_mode()->set_response_body_mode(
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::STREAMED);
      }
      ext_proc.mutable_metadata_options()->mutable_forwarding_namespaces()->add_untyped(
          "envoy.ai.token_usage");
      ext_proc.mutable_metadata_options()->mutable_forwarding_namespaces()->add_typed(
          "envoy.ai.token_usage");

      envoy::config::listener::v3::Filter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      std::ignore = ext_proc_filter.mutable_typed_config()->PackFrom(ext_proc);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
    });

    initialize();
  }

  void runJsonUsageRequest(bool end_with_trailers = false) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/v1/messages"},
                                       {":scheme", "http"},
                                       {":authority", "sni.lyft.com"}},
        "{}");
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
        false);
    upstream_request_->encodeData("{\"type\":\"message\",\"model\":\"claude-opus-5\","
                                  "\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}",
                                  /*end_stream=*/!end_with_trailers);
    if (end_with_trailers) {
      // The body ends via trailers: the filter finalizes and publishes on the
      // encodeTrailers callback, before ext_proc snapshots its
      // response_trailers message.
      Http::TestResponseTrailerMapImpl trailers{{"x-resp-trailer", "yes"}};
      upstream_request_->encodeTrailers(trailers);
    }
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  void expectUsageMetadataAtProcessor() {
    ASSERT_TRUE(metadata_seen_.WaitForNotificationWithTimeout(absl::Seconds(5)));
    EXPECT_THAT(captured_metadata_, testing::HasSubstr("\"input_tokens\":5"));
    EXPECT_THAT(captured_metadata_, testing::HasSubstr("\"output_tokens\":7"));
    EXPECT_THAT(captured_metadata_, testing::HasSubstr("\"total_tokens\":12"));
    EXPECT_THAT(captured_metadata_, testing::HasSubstr("\"api_format\":\"anthropic\""));
    // The authoritative typed record arrives through the typed forwarding
    // namespace with full integer fidelity.
    EXPECT_EQ(captured_typed_usage_.input_tokens().value(), 5);
    EXPECT_EQ(captured_typed_usage_.output_tokens().value(), 7);
    EXPECT_EQ(captured_typed_usage_.total_tokens().value(), 12);
    EXPECT_EQ(captured_typed_usage_.api_format(), envoy::data::ai::v3::TokenUsage::ANTHROPIC);
    EXPECT_EQ(captured_typed_usage_.extraction_status(), envoy::data::ai::v3::TokenUsage::COMPLETE);
  }

  Extensions::HttpFilters::ExternalProcessing::TestProcessor test_processor_;
  absl::Notification metadata_seen_;
  std::string captured_metadata_;
  envoy::data::ai::v3::TokenUsage captured_typed_usage_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AiProtocolManagerExtProcIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AiProtocolManagerExtProcIntegrationTest, DownstreamMetadataForwardedToExtProc) {
  initializeWithExtProc(/*upstream_filter=*/false);
  runJsonUsageRequest();
  expectUsageMetadataAtProcessor();
}

TEST_P(AiProtocolManagerExtProcIntegrationTest, UpstreamMetadataForwardedToExtProc) {
  initializeWithExtProc(/*upstream_filter=*/true);
  runJsonUsageRequest();
  expectUsageMetadataAtProcessor();
}

// A trailer-ended response delivers the metadata via the response_trailers
// message (trailer mode SEND); with body mode NONE no other ext_proc message
// could carry it.
TEST_P(AiProtocolManagerExtProcIntegrationTest, TrailerEndedResponseForwardsMetadata) {
  initializeWithExtProc(/*upstream_filter=*/false, /*trailer_mode=*/true);
  runJsonUsageRequest(/*end_with_trailers=*/true);
  expectUsageMetadataAtProcessor();
}

TEST_P(AiProtocolManagerExtProcIntegrationTest, UpstreamTrailerEndedResponseForwardsMetadata) {
  initializeWithExtProc(/*upstream_filter=*/true, /*trailer_mode=*/true);
  runJsonUsageRequest(/*end_with_trailers=*/true);
  expectUsageMetadataAtProcessor();
}

} // namespace
} // namespace Envoy
