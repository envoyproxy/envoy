#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

class McpFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  McpFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void initializeFilter(const std::string& config = "") {
    const std::string filter_config = config.empty() ? R"EOF(
      name: envoy.filters.http.mcp
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
        traffic_mode: PASS_THROUGH
    )EOF"
                                                     : config;

    config_helper_.prependFilter(filter_config);
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, McpFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test that a non-POST request is ignored and passes through.
TEST_P(McpFilterIntegrationTest, NonPostRequestIgnored) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}},
      0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that a valid JSON-RPC POST request passes through successfully.
TEST_P(McpFilterIntegrationTest, ValidJsonRpcPostRequest) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string request_body = R"({"jsonrpc": "2.0", "method": "test"})";
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_body, upstream_request_->body().toString());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that an MCP request with malformed JSON is rejected with a 400.
TEST_P(McpFilterIntegrationTest, InvalidJsonBodyRejected) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      R"({"jsonrpc": "2.0",)"); // Malformed JSON

  ASSERT_TRUE(response->waitForEndStream());
  // The upstream should NOT receive a request because the filter sends a local reply.
  EXPECT_FALSE(upstream_request_ != nullptr);
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test no-MCP traffic is passed through without the JSON_RPC 2.0
TEST_P(McpFilterIntegrationTest, MissingJsonRpcFieldPass) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      R"({"method": "test"})");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test no-MCP traffic is passed through without both accept headers
TEST_P(McpFilterIntegrationTest, NoAcceptHeaderPassThrough) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"method": "test"})");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that a POST request with the wrong content type is ignored and passes through.
TEST_P(McpFilterIntegrationTest, WrongContentTypePostRequestIgnored) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string request_body = R"({"jsonrpc": "2.0", "method": "test"})";
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "text/plain"}}, // Incorrect content type
      request_body);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test no-MCP traffic is passed through without both accept headers
TEST_P(McpFilterIntegrationTest, NoAcceptHeaderReject) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
      traffic_mode: REJECT_NO_MCP
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"method": "test"})");

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Only MCP"));
}

// Test REJECT_NO_MCP mode - non-MCP traffic rejected
TEST_P(McpFilterIntegrationTest, RejectNoMcpModeRejectsNonMcp) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
      traffic_mode: REJECT_NO_MCP
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Regular GET request should be rejected
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}},
      "");

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Only MCP"));
}

// Test REJECT_NO_MCP mode - SSE request passes
TEST_P(McpFilterIntegrationTest, RejectNoMcpModeAllowsSseRequest) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
      traffic_mode: REJECT_NO_MCP
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // GET request with SSE Accept header
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "text/event-stream"}},
      "");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", "text/event-stream"}, {"cache-control", "no-cache"}},
      false);

  upstream_request_->encodeData("data: {\"type\": \"message\"}\n\n", true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test REJECT_NO_MCP mode - invalid JSON-RPC rejected
TEST_P(McpFilterIntegrationTest, RejectNoMcpModeRejectsInvalidJsonRpc) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
      traffic_mode: REJECT_NO_MCP
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Invalid JSON-RPC (wrong version)
  const std::string request_body = R"({
    "jsonrpc": "1.0",
    "method": "test",
    "id": 1
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("JSON-RPC 2.0"));
}

// Test Accept header with multiple values including SSE
TEST_P(McpFilterIntegrationTest, AcceptHeaderWithMultipleValues) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
      traffic_mode: REJECT_NO_MCP
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // GET request with multiple Accept values including SSE
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json, text/event-stream, */*"}},
      "");

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test per-route override to REJECT_NO_MCP
TEST_P(McpFilterIntegrationTest, PerRouteOverrideToReject) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
      traffic_mode: PASS_THROUGH
  )EOF");

  // Configure specific route to reject non-MCP
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        route->mutable_match()->set_path("/api/mcp");

        envoy::extensions::filters::http::mcp::v3::McpOverride mcp_override;
        mcp_override.set_traffic_mode(
            envoy::extensions::filters::http::mcp::v3::Mcp::REJECT_NO_MCP);
        (*route->mutable_typed_per_filter_config())["envoy.filters.http.mcp"].PackFrom(
            mcp_override);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request to /api/mcp should be rejected (route override)
  auto response1 = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/api/mcp"}, {":scheme", "http"}, {":authority", "host"}},
      "");

  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response1->headers().getStatusValue());

  // Request to other paths should pass through
  auto response2 = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/other"}, {":scheme", "http"}, {":authority", "host"}},
      "");

  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  // route_not_found
  EXPECT_EQ("404", response2->headers().getStatusValue());
}

// Test that the filter can be disabled per-route using FilterConfig wrapper
TEST_P(McpFilterIntegrationTest, PerRouteDisabled) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
  )EOF");

  // Configure route with MCP filter disabled using FilterConfig wrapper
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);

        // Create FilterConfig wrapper with disabled=true
        envoy::config::route::v3::FilterConfig filter_config;
        filter_config.set_disabled(true);

        // Set the config to McpOverride (even though we're disabling)
        envoy::extensions::filters::http::mcp::v3::McpOverride mcp_per_route;
        filter_config.mutable_config()->PackFrom(mcp_per_route);

        (*route->mutable_typed_per_filter_config())["envoy.filters.http.mcp"].PackFrom(
            filter_config);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send invalid MCP request - should pass through because filter is disabled
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"invalid": "not-jsonrpc"})");

  waitForNextUpstreamRequest();
  EXPECT_EQ(R"({"invalid": "not-jsonrpc"})", upstream_request_->body().toString());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test virtual host level per-route config
TEST_P(McpFilterIntegrationTest, PerRouteVirtualHostLevel) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
  )EOF");

  // Disable MCP at virtual host level
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);

        envoy::config::route::v3::FilterConfig vhost_filter_config;
        vhost_filter_config.set_disabled(true);
        envoy::extensions::filters::http::mcp::v3::McpOverride vhost_mcp_per_route;
        vhost_filter_config.mutable_config()->PackFrom(vhost_mcp_per_route);
        (*virtual_host->mutable_typed_per_filter_config())["envoy.filters.http.mcp"].PackFrom(
            vhost_filter_config);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Invalid MCP request should pass through - filter disabled at vhost level
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/any"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"not": "valid-jsonrpc"})");

  waitForNextUpstreamRequest();
  EXPECT_EQ(R"({"not": "valid-jsonrpc"})", upstream_request_->body().toString());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
