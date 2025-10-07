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

  void initializeFilter() {
    config_helper_.prependFilter(R"EOF(
      name: envoy.filters.http.mcp
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
    )EOF");
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

// Test that a request with malformed JSON is rejected with a 400.
TEST_P(McpFilterIntegrationTest, InvalidJsonBodyRejected) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"jsonrpc": "2.0",)"); // Malformed JSON

  ASSERT_TRUE(response->waitForEndStream());
  // The upstream should NOT receive a request because the filter sends a local reply.
  EXPECT_FALSE(upstream_request_ != nullptr);
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test that a request with valid JSON but missing the required fields is rejected with a 400.
TEST_P(McpFilterIntegrationTest, MissingJsonRpcFieldRejected) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"method": "test"})");

  ASSERT_TRUE(response->waitForEndStream());
  // The upstream should NOT receive a request.
  EXPECT_FALSE(upstream_request_ != nullptr);
  EXPECT_EQ("400", response->headers().getStatusValue());
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
