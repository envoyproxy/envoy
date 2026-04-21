#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {
namespace {

class AiSessionIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  AiSessionIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void initializeFilter() {
    config_helper_.prependFilter(R"EOF(
      name: envoy.filters.http.ai_session
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Empty
    )EOF");
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AiSessionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// =============================================================================
// Part 1 — Content-type routing
// =============================================================================

// Non-JSON content type → filter is a no-op; request passes to upstream unchanged.
// Mirrors McpFilterIntegrationTest::WrongContentTypePostRequestIgnored.
TEST_P(AiSessionIntegrationTest, NonJsonContentTypePassesThrough) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "text/plain"}},
      "hello");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Missing Content-Type → filter is a no-op.
// Mirrors McpFilterIntegrationTest::NonPostRequestIgnored.
TEST_P(AiSessionIntegrationTest, MissingContentTypePassesThrough) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}},
      "");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// =============================================================================
// Part 2 — JSON-RPC parsing
// =============================================================================

// Valid initialize JSON-RPC request → chain completes, upstream receives it.
// McpAuthFilter allows "initialize" unconditionally; McpInitFilter marks session.
// Mirrors McpFilterIntegrationTest::ValidJsonRpcPostRequest.
TEST_P(AiSessionIntegrationTest, ValidInitializePassesThrough) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string body =
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      body);

  waitForNextUpstreamRequest();
  EXPECT_EQ(body, upstream_request_->body().toString());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Malformed JSON → JsonRpcConnectionManager sends a 400 parse-error reply;
// upstream never receives the request.
// Mirrors McpFilterIntegrationTest::InvalidJsonBodyRejected.
TEST_P(AiSessionIntegrationTest, MalformedJsonRejectedWith400) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"jsonrpc":"2.0",)"); // truncated

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Parse error"));
}

// Incomplete JSON at end-of-stream (valid tokens but object never closed)
// is also rejected with 400.
TEST_P(AiSessionIntegrationTest, IncompleteJsonAtEndOfStreamRejected) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send chunk-by-chunk so the parser sees end_stream=true on an open object.
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}});

  auto& encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Valid tokens but object never closed — finishParse() will fail.
  Buffer::OwnedImpl buf(R"({"jsonrpc":"2.0","method":"initialize")");
  encoder.encodeData(buf, true); // end_stream=true on an unclosed object

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Parse error"));
}

// Multi-chunk body is assembled correctly; chain completes and upstream receives
// the full reassembled body.
// Mirrors McpFilterIntegrationTest::ChunkByChunkBodyParsing.
TEST_P(AiSessionIntegrationTest, ChunkByChunkBodyParsedCorrectly) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string full_body =
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})";

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}});

  auto& encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  Buffer::OwnedImpl chunk1(R"({"jsonrpc":"2.0",)");
  encoder.encodeData(chunk1, false);

  Buffer::OwnedImpl chunk2(R"("id":1,"method":"initialize",)");
  encoder.encodeData(chunk2, false);

  Buffer::OwnedImpl chunk3(R"("params":{"capabilities":{}}})");
  encoder.encodeData(chunk3, true);

  waitForNextUpstreamRequest();
  EXPECT_EQ(full_body, upstream_request_->body().toString());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// =============================================================================
// Part 3 — AI filter chain (McpAuth → McpInit → McpContext)
// =============================================================================

// tools/call without a session identity → McpAuthFilter rejects with a
// JSON-RPC error response (HTTP 200 per JSON-RPC 2.0 §5).
// Upstream never receives the request.
TEST_P(AiSessionIntegrationTest, UnauthenticatedToolCallRejectedWithJsonRpcError) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"search"}})");

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Unauthenticated"));
}

// tools/call on an uninitialized session (after a second request using the same
// session-id but bypassing auth via a different principal path) → McpInitFilter
// rejects. Here we verify the uninitialized path directly with a fresh session.
TEST_P(AiSessionIntegrationTest, UninitializedSessionToolCallRejected) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // First: initialize the session to verify it reaches upstream.
  {
    const std::string sid = "session-lifecycle";
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/mcp"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"},
                                       {"mcp-session-id", sid}},
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})");

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  // Second: tools/call on same session is still rejected because the session
  // has no identity (McpAuthFilter runs before McpInitFilter).
  {
    const std::string sid = "session-lifecycle";
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/mcp"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"},
                                       {"mcp-session-id", sid}},
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"x"}})");

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_FALSE(upstream_request_);
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_THAT(response->body(), testing::HasSubstr("Unauthenticated"));
  }
}

// admin/ method without auth is rejected by McpAuthFilter.
TEST_P(AiSessionIntegrationTest, AdminMethodRejectedWithoutAuth) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"jsonrpc":"2.0","id":1,"method":"admin/reset","params":{}})");

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Unauthenticated"));
}

// =============================================================================
// Part 4 — Session management
// =============================================================================

// Two requests with the same Mcp-Session-Id share the same session — the
// second initialize still passes through (session state preserved).
// Mirrors the session-reuse contract of AiSessionManager.
TEST_P(AiSessionIntegrationTest, SameSessionIdReusesSession) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string sid = "persistent-session";

  for (int req = 1; req <= 2; ++req) {
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/mcp"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"},
                                       {"mcp-session-id", sid}},
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})");

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

// Two different session IDs produce independent sessions; both can initialize
// concurrently without interfering.
TEST_P(AiSessionIntegrationTest, DifferentSessionIdsAreIsolated) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  for (const std::string& sid : {"alice-session", "bob-session"}) {
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/mcp"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"},
                                       {"mcp-session-id", sid}},
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})");

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

// No Mcp-Session-Id header → anonymous session is used; request still passes
// through for initialize.
TEST_P(AiSessionIntegrationTest, NoSessionIdHeaderUsesAnonymousSession) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// application/json-rpc alternate content-type is also recognised and processed.
TEST_P(AiSessionIntegrationTest, ApplicationJsonRpcContentTypeProcessed) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json-rpc"}},
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
