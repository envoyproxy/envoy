#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"
#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"

#include "source/common/common/base64.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

class McpRouterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  McpRouterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void createUpstreams() override {
    // Create two fake upstreams for MCP backends (time and tools)
    for (int i = 0; i < 2; ++i) {
      addFakeUpstream(Http::CodecType::HTTP1);
    }
  }

  void initializeFilter() {
    config_helper_.skipPortUsageValidation();

    // Add both clusters for MCP backends
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // First cluster: mcp_time_backend (uses fake_upstreams_[0])
      auto* time_cluster = bootstrap.mutable_static_resources()->add_clusters();
      time_cluster->set_name("mcp_time_backend");
      time_cluster->mutable_connect_timeout()->set_seconds(5);
      time_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      time_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

      auto* time_endpoint = time_cluster->mutable_load_assignment();
      time_endpoint->set_cluster_name("mcp_time_backend");
      auto* time_locality = time_endpoint->add_endpoints();
      auto* time_lb = time_locality->add_lb_endpoints();
      time_lb->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
          Network::Test::getLoopbackAddressString(GetParam()));
      time_lb->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(
          fake_upstreams_[0]->localAddress()->ip()->port());

      // Second cluster: mcp_tools_backend (uses fake_upstreams_[1])
      auto* tools_cluster = bootstrap.mutable_static_resources()->add_clusters();
      tools_cluster->set_name("mcp_tools_backend");
      tools_cluster->mutable_connect_timeout()->set_seconds(5);
      tools_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      tools_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

      auto* tools_endpoint = tools_cluster->mutable_load_assignment();
      tools_endpoint->set_cluster_name("mcp_tools_backend");
      auto* tools_locality = tools_endpoint->add_endpoints();
      auto* tools_lb = tools_locality->add_lb_endpoints();
      tools_lb->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
          Network::Test::getLoopbackAddressString(GetParam()));
      tools_lb->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(
          fake_upstreams_[1]->localAddress()->ip()->port());
    });

    // MCP router as terminal filter
    config_helper_.prependFilter(R"EOF(
      name: envoy.filters.http.mcp_router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_router.v3.McpRouter
        servers:
          - name: time
            mcp_cluster:
              cluster: mcp_time_backend
              path: /mcp
              timeout: 5s
              host_rewrite_literal: time.mcp.example.com
          - name: tools
            mcp_cluster:
              cluster: mcp_tools_backend
              path: /mcp
              timeout: 5s
              host_rewrite_literal: tools.mcp.example.com
    )EOF");

    // MCP filter (validates JSON-RPC, sets metadata)
    config_helper_.prependFilter(R"EOF(
      name: envoy.filters.http.mcp
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
        traffic_mode: PASS_THROUGH
    )EOF");

    // Remove the default router filter.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* filters = hcm.mutable_http_filters();
          for (auto it = filters->begin(); it != filters->end();) {
            if (it->name() == "envoy.filters.http.router") {
              it = filters->erase(it);
            } else {
              ++it;
            }
          }
        });

    initialize();
  }

  void TearDown() override { cleanupUpstreamAndDownstream(); }

  FakeHttpConnectionPtr time_backend_connection_;
  FakeHttpConnectionPtr tools_backend_connection_;
  FakeStreamPtr time_backend_request_;
  FakeStreamPtr tools_backend_request_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, McpRouterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test that ping request returns JSON-RPC response with empty result
TEST_P(McpRouterIntegrationTest, PingReturnsEmptyResult) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "ping",
    "id": 3
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  // Per MCP spec: ping must respond with empty result
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("\"jsonrpc\":\"2.0\""));
  EXPECT_THAT(response->body(), testing::HasSubstr("\"id\":3"));
  EXPECT_THAT(response->body(), testing::HasSubstr("\"result\":{}"));
}

// Test notifications/initialized request returns 202 Accepted
TEST_P(McpRouterIntegrationTest, NotificationInitializedReturns202) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "notifications/initialized"
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  // Notification should return 202 Accepted immediately
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("202", response->headers().getStatusValue());
}

// Test invalid JSON returns 400
TEST_P(McpRouterIntegrationTest, InvalidJsonReturns400) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Invalid JSON
  const std::string request_body = R"({invalid json)";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  // Invalid JSON should be rejected
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test initialize request fans out to both backends and aggregates responses
TEST_P(McpRouterIntegrationTest, InitializeFanoutToBothBackends) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "initialize",
    "id": 1,
    "params": {
      "protocolVersion": "2025-06-18",
      "capabilities": {},
      "clientInfo": {"name": "test-client", "version": "1.0"}
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  // Wait for request on time backend (fake_upstreams_[0])
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  EXPECT_EQ("time.mcp.example.com", time_backend_request_->headers().getHostValue());

  // Wait for request on tools backend (fake_upstreams_[1])
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  EXPECT_EQ("tools.mcp.example.com", tools_backend_request_->headers().getHostValue());

  // Send response from time backend with session ID
  const std::string time_response = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "protocolVersion": "2025-06-18",
      "serverInfo": {"name": "time-server", "version": "1.0"},
      "capabilities": {"tools": {}}
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"mcp-session-id", "time-session-123"}},
      false);
  Buffer::OwnedImpl time_body(time_response);
  time_backend_request_->encodeData(time_body, true);

  // Send response from tools backend with session ID
  const std::string tools_response = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "protocolVersion": "2025-06-18",
      "serverInfo": {"name": "tools-server", "version": "1.0"},
      "capabilities": {"tools": {}}
    }
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"mcp-session-id", "tools-session-456"}},
      false);
  Buffer::OwnedImpl tools_body(tools_response);
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify response has mcp-session-id header (composite session)
  auto session_header = response->headers().get(Http::LowerCaseString("mcp-session-id"));
  EXPECT_FALSE(session_header.empty());

  // Decode the composite session ID and verify it contains both backend sessions
  std::string encoded_session = std::string(session_header[0]->value().getStringView());
  std::string decoded_session = Base64::decode(encoded_session);
  EXPECT_FALSE(decoded_session.empty());

  // Verify the session contains the expected backend session IDs (doubly Base64 encoded)
  // Format: {route}@{subject}@{backend1}:{base64(sid1)},{backend2}:{base64(sid2)}
  std::string time_session_base64 = Base64::encode("time-session-123", strlen("time-session-123"));
  std::string tools_session_base64 =
      Base64::encode("tools-session-456", strlen("tools-session-456"));
  EXPECT_THAT(decoded_session, testing::HasSubstr("time:" + time_session_base64));
  EXPECT_THAT(decoded_session, testing::HasSubstr("tools:" + tools_session_base64));

  EXPECT_THAT(response->body(), testing::HasSubstr("protocolVersion"));
  EXPECT_THAT(response->body(), testing::HasSubstr("envoy-mcp-gateway"));
}

// Test tools/list request fans out to both backends and aggregates tools with prefixes
TEST_P(McpRouterIntegrationTest, ToolsListFanoutAggregation) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/list",
    "id": 2
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  const std::string time_response = R"({
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
      "tools": [
        {"name": "get_current_time", "description": "Get the current time"}
      ]
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_body(time_response);
  time_backend_request_->encodeData(time_body, true);

  const std::string tools_response = R"({
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
      "tools": [
        {"name": "calculator", "description": "Perform calculations"},
        {"name": "converter", "description": "Convert units"}
      ]
    }
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_body(tools_response);
  tools_backend_request_->encodeData(tools_body, true);

  // Wait for aggregated response
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify the aggregated response contains tools from both backends with prefixes
  // Since we have 2 backends, tools should be prefixed with backend names
  EXPECT_THAT(response->body(), testing::HasSubstr("time__get_current_time"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__calculator"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__converter"));
}

// Test tools/call request routes to correct backend and strips prefix
TEST_P(McpRouterIntegrationTest, ToolCallRoutesToCorrectBackend) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "id": 1,
    "params": {
      "name": "time__get_current_time",
      "arguments": {}
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  // Request should be routed to time backend (fake_upstreams_[0]) based on "time__" prefix
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify upstream request body has prefix stripped
  EXPECT_THAT(time_backend_request_->body().toString(),
              testing::HasSubstr("\"name\": \"get_current_time\""));
  EXPECT_THAT(time_backend_request_->body().toString(), testing::Not(testing::HasSubstr("time__")));

  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "content": [{"type": "text", "text": "2023-10-27T10:00:00Z"}]
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("2023-10-27T10:00:00Z"));
}

// Test tools/call routes to the second backend (tools) based on prefix
TEST_P(McpRouterIntegrationTest, ToolCallToSecondBackend) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "id": 5,
    "params": {
      "name": "tools__calculator",
      "arguments": {"a": 1, "b": 2}
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  // Request should be routed to tools backend (fake_upstreams_[1]) based on "tools__" prefix
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  // Verify routing to correct backend via host header
  EXPECT_EQ("tools.mcp.example.com", tools_backend_request_->headers().getHostValue());

  // Verify upstream request body has prefix stripped
  EXPECT_THAT(tools_backend_request_->body().toString(),
              testing::HasSubstr("\"name\": \"calculator\""));
  EXPECT_THAT(tools_backend_request_->body().toString(),
              testing::Not(testing::HasSubstr("tools__")));

  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 5,
    "result": {
      "content": [{"type": "text", "text": "3"}]
    }
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  tools_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("\"text\": \"3\""));
}

// Test session ID from initialize is propagated in subsequent requests
TEST_P(McpRouterIntegrationTest, SessionIdPropagation) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Step 1: Initialize to get session ID
  const std::string init_body = R"({
    "jsonrpc": "2.0",
    "method": "initialize",
    "id": 1,
    "params": {
      "protocolVersion": "2025-06-18",
      "capabilities": {},
      "clientInfo": {"name": "test-client", "version": "1.0"}
    }
  })";

  auto init_response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      init_body);

  // Handle both backends for initialize
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  // Send responses from both backends with session IDs
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"mcp-session-id", "time-session-abc"}},
      false);
  Buffer::OwnedImpl time_body(
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"time","version":"1.0"},"capabilities":{}}})");
  time_backend_request_->encodeData(time_body, true);

  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"mcp-session-id", "tools-session-xyz"}},
      false);
  Buffer::OwnedImpl tools_body(
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"tools","version":"1.0"},"capabilities":{}}})");
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(init_response->waitForEndStream());
  EXPECT_EQ("200", init_response->headers().getStatusValue());

  auto session_header = init_response->headers().get(Http::LowerCaseString("mcp-session-id"));
  ASSERT_FALSE(session_header.empty());
  std::string composite_session = std::string(session_header[0]->value().getStringView());
  EXPECT_FALSE(composite_session.empty());

  // Step 2: Make a tools/call with the session ID
  const std::string call_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "id": 2,
    "params": {
      "name": "time__get_current_time",
      "arguments": {}
    }
  })";

  FakeStreamPtr time_backend_request2;
  auto call_response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"mcp-session-id", composite_session}},
      call_body);

  // Verify request goes to time backend with the per-backend session ID
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request2));
  ASSERT_TRUE(time_backend_request2->waitForEndStream(*dispatcher_));

  auto backend_session =
      time_backend_request2->headers().get(Http::LowerCaseString("mcp-session-id"));
  ASSERT_FALSE(backend_session.empty());
  EXPECT_EQ("time-session-abc", backend_session[0]->value().getStringView());

  time_backend_request2->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl call_response_body(
      R"({"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"2023-12-10T00:00:00Z"}]}})");
  time_backend_request2->encodeData(call_response_body, true);

  ASSERT_TRUE(call_response->waitForEndStream());
  EXPECT_EQ("200", call_response->headers().getStatusValue());
}

// Test GET method returns 405 Method Not Allowed
TEST_P(McpRouterIntegrationTest, GETMethodReturns405) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"}});

  // GET method should return 405 Method Not Allowed
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("405", response->headers().getStatusValue());
}

// Test tools/call with unknown backend prefix returns 400
TEST_P(McpRouterIntegrationTest, ToolCallWithUnknownBackendReturns400) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Use a tool name with an unknown backend prefix
  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "id": 10,
    "params": {
      "name": "unknown_backend__some_tool",
      "arguments": {}
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      request_body);

  // Unknown backend prefix should return 400 Bad Request
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
