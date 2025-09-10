#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/http/sse_stateful_session.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpSseStatefulSession {
namespace {

class StatefulSessionIntegrationTest : public Envoy::HttpIntegrationTest, public testing::Test {
public:
  StatefulSessionIntegrationTest()
      : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {
    // Create 4 different upstream server for stateful session test.
    setUpstreamCount(4);

    // Update endpoints of default cluster `cluster_0` to 4 different fake upstreams.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT(cluster_0->name() == "cluster_0");
      auto* endpoint = cluster_0->mutable_load_assignment()->mutable_endpoints()->Mutable(0);

      const std::string EndpointsYaml = R"EOF(
        lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
      )EOF";

      envoy::config::endpoint::v3::LocalityLbEndpoints new_lb_endpints;
      TestUtility::loadFromYaml(EndpointsYaml, new_lb_endpints);
      *endpoint = new_lb_endpints;
    });
  }

  // Initialize route filter and per route config.
  void initializeFilterAndRoute(const std::string& filter_yaml,
                                const std::string& per_route_config_yaml) {
    config_helper_.prependFilter(filter_yaml);

    // Create virtual host with domain `stateful.session.com` and default route to `cluster_0`
    auto virtual_host = config_helper_.createVirtualHost("stateful.session.com");

    // Update per route config of default route.
    if (!per_route_config_yaml.empty()) {
      auto* route = virtual_host.mutable_routes(0);
      Protobuf::Any per_route_config;
      TestUtility::loadFromYaml(per_route_config_yaml, per_route_config);

      route->mutable_typed_per_filter_config()->insert(
          {"envoy.filters.http.mcp_sse_stateful_session", per_route_config});
    }
    config_helper_.addVirtualHost(virtual_host);

    initialize();
  }
};

static const std::string STATEFUL_SESSION_FILTER =
    R"EOF(
name: envoy.filters.http.mcp_sse_stateful_session
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_sse_stateful_session.v3alpha.McpSseStatefulSession
  sse_session_state:
    name: envoy.http.sse_stateful_session.envelope
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.mcp_sse_stateful_session.envelope.v3alpha.EnvelopeSessionState
      param_name: sessionId
      chunk_end_patterns: ["\r\n\r\n", "\n\n", "\r\r"]
)EOF";

static const std::string STATEFUL_SESSION_STRICT_MODE =
    R"EOF(
  strict: true
)EOF";

static const std::string DISABLE_STATEFUL_SESSION =
    R"EOF(
"@type": type.googleapis.com/envoy.extensions.filters.http.mcp_sse_stateful_session.v3alpha.McpSseStatefulSessionPerRoute
disabled: true
)EOF";

static const std::string EMPTY_STATEFUL_SESSION =
    R"EOF(
name: envoy.filters.http.mcp_sse_stateful_session
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_sse_stateful_session.v3alpha.McpSseStatefulSession
  strict: false
)EOF";

static const std::string OVERRIDE_STATEFUL_SESSION =
    R"EOF(
"@type": type.googleapis.com/envoy.extensions.filters.http.mcp_sse_stateful_session.v3alpha.McpSseStatefulSessionPerRoute
mcp_sse_stateful_session:
  sse_session_state:
    name: envoy.http.sse_stateful_session.envelope
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.mcp_sse_stateful_session.envelope.v3alpha.EnvelopeSessionState
      param_name: sessionId
      chunk_end_patterns: ["\r\n\r\n", "\n\n", "\r\r"]
  strict: true
)EOF";

// Tests upstream SSE response injection in Envelope + Strict mode.
// Verifies that session host address is correctly encoded and injected into SSE stream.
TEST_F(StatefulSessionIntegrationTest, McpSseStatefulSessionEnvelopeSseStrictMode) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER + STATEFUL_SESSION_STRICT_MODE, "");

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Construct SSE request
  Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{{":method", "GET"},
                                                            {":path", "/sse"},
                                                            {":scheme", "http"},
                                                            {":authority", "stateful.session.com"}};

  auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

  // Wait for upstream request
  auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
  ASSERT(upstream_index.has_value());

  envoy::config::endpoint::v3::LbEndpoint endpoint;
  setUpstreamAddress(upstream_index.value(), endpoint);
  const std::string address_string =
      fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
  const std::string encoded_host =
      Envoy::Base64Url::encode(address_string.data(), address_string.size());

  // Set content type to text/event-stream (required for SSE)
  default_response_headers_.addCopy(Envoy::Http::LowerCaseString("content-type"),
                                    "text/event-stream");
  upstream_request_->encodeHeaders(default_response_headers_, false); // stream not closed yet

  // Build and send initial SSE event data
  const std::string original_session_id = "abcdefg";
  const std::string sse_data =
      fmt::format("data: https://example.com/test?sessionId={}\n\n", original_session_id);
  upstream_request_->encodeData(sse_data, true);
  ASSERT_TRUE(sse_response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(sse_response->complete());

  // Build expected response with host address
  const std::string expected_sse_data =
      "data: https://example.com/test?sessionId=abcdefg." + encoded_host + "\n\n";

  EXPECT_EQ(expected_sse_data, sse_response->body());

  cleanupUpstreamAndDownstream();
}

// Test for downstream request with stateful session envelope SSE and strict mode.
// The request should be routed to the upstream server based on the encoded session ID in the SSE
// request. The test checks that the correct upstream server is selected based on the session ID
// and that the response is correctly formatted as an SSE event.
// It also checks that the strict mode works correctly by returning 503 for unknown server
// addresses.
TEST_F(StatefulSessionIntegrationTest,
       DownstreamRequestWithMcpSseStatefulSessionEnvelopeAndStrictMode) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER + STATEFUL_SESSION_STRICT_MODE, "");
  // Upstream endpoint encoded in stateful session SSE points to the first server address.
  // This should return the first server address.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());

    // Encode upstream address using Base64Url
    const std::string encoded_host =
        Envoy::Base64Url::encode(address_string.data(), address_string.size());
    const std::string session_param = "abcdefg." + encoded_host;

    // Construct SSE request with encoded session parameter
    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", fmt::format("/sse?sessionId={}", session_param)},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Wait for upstream request
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT_TRUE(upstream_index.has_value());

    // Expect that the selected upstream is index 1
    EXPECT_EQ(upstream_index.value(), 1);

    // Send response headers and complete stream
    default_response_headers_.addCopy(Envoy::Http::LowerCaseString("content-type"),
                                      "text/event-stream");
    upstream_request_->encodeHeaders(default_response_headers_, false);
    upstream_request_->encodeData("data: hello\n\n", true);

    ASSERT_TRUE(sse_response->waitForEndStream());

    cleanupUpstreamAndDownstream();
  }
  // Upstream endpoint encoded in stateful session SSE points to the second server address.
  // This should return the second server address.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(2, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());

    // Encode upstream address using Base64Url
    const std::string encoded_host =
        Envoy::Base64Url::encode(address_string.data(), address_string.size());
    const std::string session_param = "abcdefg." + encoded_host;

    // Construct SSE request with encoded session parameter
    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", fmt::format("/sse?sessionId={}", session_param)},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Wait for upstream request
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT_TRUE(upstream_index.has_value());

    // Expect that the selected upstream is index 2
    EXPECT_EQ(upstream_index.value(), 2);

    // Send response headers and complete stream
    default_response_headers_.addCopy(Envoy::Http::LowerCaseString("content-type"),
                                      "text/event-stream");
    upstream_request_->encodeHeaders(default_response_headers_, false);
    upstream_request_->encodeData("data: hello\n\n", true);

    ASSERT_TRUE(sse_response->waitForEndStream());

    cleanupUpstreamAndDownstream();
  }
  // Upstream endpoint encoded in stateful session SSE points to unknown server address.
  // This should return 503.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // This decodes to "127.0.0.9:50000"
    const std::string host = "127.0.0.9:50000";
    const std::string encoded_host = Envoy::Base64Url::encode(host.data(), host.size());
    const std::string session_param = "abcdefg." + encoded_host;

    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", fmt::format("/sse?sessionId={}", session_param)},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Should return 503 because the host is unknown
    ASSERT_TRUE(sse_response->waitForEndStream());
    EXPECT_EQ("503", sse_response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }
}

TEST_F(StatefulSessionIntegrationTest, StatefulSessionDisabledByRoute) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, DISABLE_STATEFUL_SESSION);

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // Construct SSE request
    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", "/sse"},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Wait for upstream request
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT(upstream_index.has_value());

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(upstream_index.value(), endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_host =
        Envoy::Base64Url::encode(address_string.data(), address_string.size());

    // Set content type to text/event-stream (required for SSE)
    default_response_headers_.addCopy(Envoy::Http::LowerCaseString("content-type"),
                                      "text/event-stream");
    upstream_request_->encodeHeaders(default_response_headers_, false); // stream not closed yet

    // Build and send initial SSE event data
    const std::string original_session_id = "abcdefg";
    const std::string sse_data =
        fmt::format("data: https://example.com/test?sessionId={}\n\n", original_session_id);
    upstream_request_->encodeData(sse_data, true);
    ASSERT_TRUE(sse_response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(sse_response->complete());

    // Build expected response with host address
    const std::string expected_sse_data = "data: https://example.com/test?sessionId=abcdefg\n\n";

    EXPECT_EQ(expected_sse_data, sse_response->body());

    cleanupUpstreamAndDownstream();
  }
}

// Empty stateful session should be overridden by per route config.
TEST_F(StatefulSessionIntegrationTest, StatefulSessionOverrideByRoute) {
  initializeFilterAndRoute(EMPTY_STATEFUL_SESSION, OVERRIDE_STATEFUL_SESSION);
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // Construct SSE request
    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", "/sse"},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Wait for upstream request
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT(upstream_index.has_value());

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(upstream_index.value(), endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_host =
        Envoy::Base64Url::encode(address_string.data(), address_string.size());

    // Set content type to text/event-stream (required for SSE)
    default_response_headers_.addCopy(Envoy::Http::LowerCaseString("content-type"),
                                      "text/event-stream");
    upstream_request_->encodeHeaders(default_response_headers_, false); // stream not closed yet

    // Build and send initial SSE event data
    const std::string original_session_id = "abcdefg";
    const std::string sse_data =
        fmt::format("data: https://example.com/test?sessionId={}\n\n", original_session_id);
    upstream_request_->encodeData(sse_data, true);
    ASSERT_TRUE(sse_response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(sse_response->complete());

    // Build expected response with host address
    const std::string expected_sse_data =
        "data: https://example.com/test?sessionId=abcdefg." + encoded_host + "\n\n";

    EXPECT_EQ(expected_sse_data, sse_response->body());

    cleanupUpstreamAndDownstream();
  }

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());

    // Encode upstream address using Base64Url
    const std::string encoded_host =
        Envoy::Base64Url::encode(address_string.data(), address_string.size());
    const std::string session_param = "abcdefg." + encoded_host;

    // Construct SSE request with encoded session parameter
    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", fmt::format("/sse?sessionId={}", session_param)},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Wait for upstream request
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT_TRUE(upstream_index.has_value());

    // Expect that the selected upstream is index 1
    EXPECT_EQ(upstream_index.value(), 1);

    // Send response headers and complete stream
    default_response_headers_.addCopy(Envoy::Http::LowerCaseString("content-type"),
                                      "text/event-stream");
    upstream_request_->encodeHeaders(default_response_headers_, false);
    upstream_request_->encodeData("data: hello\n\n", true);

    ASSERT_TRUE(sse_response->waitForEndStream());

    cleanupUpstreamAndDownstream();
  }
  // Upstream endpoint encoded in stateful session SSE points to the second server address.
  // This should return the second server address.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(2, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());

    // Encode upstream address using Base64Url
    const std::string encoded_host =
        Envoy::Base64Url::encode(address_string.data(), address_string.size());
    const std::string session_param = "abcdefg." + encoded_host;

    // Construct SSE request with encoded session parameter
    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", fmt::format("/sse?sessionId={}", session_param)},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Wait for upstream request
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT_TRUE(upstream_index.has_value());

    // Expect that the selected upstream is index 1
    EXPECT_EQ(upstream_index.value(), 2);

    // Send response headers and complete stream
    default_response_headers_.addCopy(Envoy::Http::LowerCaseString("content-type"),
                                      "text/event-stream");
    upstream_request_->encodeHeaders(default_response_headers_, false);
    upstream_request_->encodeData("data: hello\n\n", true);

    ASSERT_TRUE(sse_response->waitForEndStream());

    cleanupUpstreamAndDownstream();
  }
  // Upstream endpoint encoded in stateful session SSE points to unknown server address.
  // This should return 503.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // This decodes to "127.0.0.9:50000"
    const std::string host = "127.0.0.9:50000";
    const std::string encoded_host = Envoy::Base64Url::encode(host.data(), host.size());
    const std::string session_param = "abcdefg." + encoded_host;

    Envoy::Http::TestRequestHeaderMapImpl sse_request_headers{
        {":method", "GET"},
        {":path", fmt::format("/sse?sessionId={}", session_param)},
        {":scheme", "http"},
        {":authority", "stateful.session.com"}};

    auto sse_response = codec_client_->makeRequestWithBody(sse_request_headers, 0);

    // Should return 503 because the host is unknown
    ASSERT_TRUE(sse_response->waitForEndStream());
    EXPECT_EQ("503", sse_response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }
}

} // namespace
} // namespace McpSseStatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
