#include <string>

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

// End-to-end coverage for the AI Protocol Manager filter. The filter offloads
// the request body into an external buffer as it arrives and replays it back
// into the filter chain once the stream ends (see filter.h). These tests drive
// real requests through a configured Envoy and assert that the upstream still
// observes the headers, the complete body (across a range of sizes), and any
// trailers unchanged -- i.e. the offload/replay round-trip is transparent.
class AiProtocolManagerIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void prependFilter() {
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
)EOF");
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
};

INSTANTIATE_TEST_SUITE_P(Protocols, AiProtocolManagerIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Headers-only request: the filter must let the headers flow immediately (there
// is no payload to offload), and the round-trip must complete normally.
TEST_P(AiProtocolManagerIntegrationTest, HeaderOnly) {
  prependFilter();
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

// Header + body: the offloaded body must be replayed in full to the upstream.
// Parameterized over a range of sizes to exercise empty, sub-chunk, and
// multi-chunk payloads through the offload/replay path.
TEST_P(AiProtocolManagerIntegrationTest, HeaderAndBody) {
  prependFilter();
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

// Header + body sent as several explicit frames before end_stream. Verifies the
// filter reassembles a body delivered across multiple decodeData() calls.
TEST_P(AiProtocolManagerIntegrationTest, HeaderAndBodyMultipleFrames) {
  prependFilter();
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

// Header + body + trailers: the stream is terminated by trailers rather than an
// end_stream data frame. The filter must replay the buffered body and then
// release the trailers, so the upstream observes both intact.
TEST_P(AiProtocolManagerIntegrationTest, HeaderAndBodyAndTrailers) {
  prependFilter();
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
TEST_P(AiProtocolManagerIntegrationTest, LocalReplyDuringReplayTearsDownCleanly) {
  // Order matters: AiProtocolManager must run first (it offloads the body), the
  // buffer filter after it (it sees the body only when the manager replays it).
  // prependFilter() inserts at the head, so add the buffer filter first.
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.buffer
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
  max_request_bytes: 1024
)EOF");
  prependFilter();
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

// Trailers immediately after headers, with no body in between.
TEST_P(AiProtocolManagerIntegrationTest, HeaderAndTrailersNoBody) {
  prependFilter();
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

} // namespace
} // namespace Envoy
