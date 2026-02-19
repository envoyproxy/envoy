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

  // Verify stats: ping is a direct response
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_total", 1);
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_direct_response", 1);
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

  // Verify stats: notification is a direct response with fanout
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_total", 1);
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_direct_response", 1);
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_fanout", 1);
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

  // Verify stats: initialize is a fanout operation
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_total", 1);
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_fanout", 1);
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

  // Verify stats: tool call with body rewrite
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_total", 1);
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_body_rewrite", 1);
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

// Test tools/call content-length is adjusted when tool name is rewritten.
TEST_P(McpRouterIntegrationTest, ToolCallContentLengthAdjustment) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body =
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"time__get_time","arguments":{}}})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"content-length", std::to_string(request_body.size())}},
      request_body);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify content-length was adjusted: original "time__get_time" -> "get_time" (7 chars removed).
  auto content_length_header = time_backend_request_->headers().ContentLength();
  ASSERT_NE(content_length_header, nullptr);
  int64_t upstream_length = 0;
  ASSERT_TRUE(absl::SimpleAtoi(content_length_header->value().getStringView(), &upstream_length));
  // Original body size minus "time__" prefix (6 chars).
  EXPECT_EQ(upstream_length, static_cast<int64_t>(request_body.size()) - 6);

  const std::string backend_response = R"({"jsonrpc":"2.0","id":1,"result":{}})";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl resp_body(backend_response);
  time_backend_request_->encodeData(resp_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test resources/read content-length is adjusted when URI is rewritten.
TEST_P(McpRouterIntegrationTest, ResourcesReadContentLengthAdjustment) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body =
      R"({"jsonrpc":"2.0","method":"resources/read","id":2,"params":{"uri":"time+file://data"}})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"content-length", std::to_string(request_body.size())}},
      request_body);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify content-length was adjusted: "time+file://data" -> "file://data" (5 chars removed).
  auto content_length_header = time_backend_request_->headers().ContentLength();
  ASSERT_NE(content_length_header, nullptr);
  int64_t upstream_length = 0;
  ASSERT_TRUE(absl::SimpleAtoi(content_length_header->value().getStringView(), &upstream_length));
  EXPECT_EQ(upstream_length, static_cast<int64_t>(request_body.size()) - 5);

  const std::string backend_response = R"({"jsonrpc":"2.0","id":2,"result":{"contents":[]}})";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl resp_body(backend_response);
  time_backend_request_->encodeData(resp_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test prompts/get content-length is adjusted when prompt name is rewritten.
TEST_P(McpRouterIntegrationTest, PromptsGetContentLengthAdjustment) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body =
      R"({"jsonrpc":"2.0","method":"prompts/get","id":3,"params":{"name":"time__greeting"}})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"content-length", std::to_string(request_body.size())}},
      request_body);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify content-length was adjusted: "time__greeting" -> "greeting" (6 chars removed).
  auto content_length_header = time_backend_request_->headers().ContentLength();
  ASSERT_NE(content_length_header, nullptr);
  int64_t upstream_length = 0;
  ASSERT_TRUE(absl::SimpleAtoi(content_length_header->value().getStringView(), &upstream_length));
  EXPECT_EQ(upstream_length, static_cast<int64_t>(request_body.size()) - 6);

  const std::string backend_response = R"({"jsonrpc":"2.0","id":3,"result":{"messages":[]}})";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl resp_body(backend_response);
  time_backend_request_->encodeData(resp_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test completion/complete with ref/prompt content-length is adjusted.
TEST_P(McpRouterIntegrationTest, CompletionCompletePromptRefContentLengthAdjustment) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body =
      R"({"jsonrpc":"2.0","method":"completion/complete","id":4,"params":{"ref":{"type":"ref/prompt","name":"time__greet"},"argument":{"name":"x","value":"y"}}})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"content-length", std::to_string(request_body.size())}},
      request_body);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify content-length was adjusted: "time__greet" -> "greet" (6 chars removed).
  auto content_length_header = time_backend_request_->headers().ContentLength();
  ASSERT_NE(content_length_header, nullptr);
  int64_t upstream_length = 0;
  ASSERT_TRUE(absl::SimpleAtoi(content_length_header->value().getStringView(), &upstream_length));
  EXPECT_EQ(upstream_length, static_cast<int64_t>(request_body.size()) - 6);

  const std::string backend_response =
      R"({"jsonrpc":"2.0","id":4,"result":{"completion":{"values":[]}}})";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl resp_body(backend_response);
  time_backend_request_->encodeData(resp_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test completion/complete with ref/resource content-length is adjusted.
TEST_P(McpRouterIntegrationTest, CompletionCompleteResourceRefContentLengthAdjustment) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body =
      R"({"jsonrpc":"2.0","method":"completion/complete","id":5,"params":{"ref":{"type":"ref/resource","uri":"time+file://x"},"argument":{"name":"a","value":"b"}}})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"content-length", std::to_string(request_body.size())}},
      request_body);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify content-length was adjusted: "time+file://x" -> "file://x" (5 chars removed).
  auto content_length_header = time_backend_request_->headers().ContentLength();
  ASSERT_NE(content_length_header, nullptr);
  int64_t upstream_length = 0;
  ASSERT_TRUE(absl::SimpleAtoi(content_length_header->value().getStringView(), &upstream_length));
  EXPECT_EQ(upstream_length, static_cast<int64_t>(request_body.size()) - 5);

  const std::string backend_response =
      R"({"jsonrpc":"2.0","id":5,"result":{"completion":{"values":[]}}})";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl resp_body(backend_response);
  time_backend_request_->encodeData(resp_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
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

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());

  // Verify stats: unknown backend
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_total", 1);
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_unknown_backend", 1);
}

// Test tools/call with SSE response from backend returns SSE to client
TEST_P(McpRouterIntegrationTest, ToolCallWithSseResponse) {
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

  // Request should be routed to time backend
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Backend responds with SSE content type
  const std::string sse_data =
      R"({"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"2023-10-27T10:00:00Z"}]}})";
  const std::string sse_response = "data: " + sse_data + "\n\n";

  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl response_body(sse_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Response should be SSE format
  EXPECT_EQ("text/event-stream", response->headers().getContentTypeValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("data:"));
  EXPECT_THAT(response->body(), testing::HasSubstr("2023-10-27T10:00:00Z"));
}

// Test tools/list aggregates SSE responses from backends into JSON
TEST_P(McpRouterIntegrationTest, ToolsListAggregatesSseResponses) {
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

  // Time backend responds with SSE
  const std::string time_json =
      R"({"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"get_current_time","description":"Get the current time"}]}})";
  const std::string time_sse = "data: " + time_json + "\n\n";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl time_body(time_sse);
  time_backend_request_->encodeData(time_body, true);

  // Tools backend responds with regular JSON
  const std::string tools_response = R"({
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
      "tools": [
        {"name": "calculator", "description": "Perform calculations"}
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

  // Aggregated response should be JSON (not SSE) and contain tools from both backends
  EXPECT_EQ("application/json", response->headers().getContentTypeValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("time__get_current_time"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__calculator"));
}

// Test SSE response with multiple data events is passed through for tools/call
TEST_P(McpRouterIntegrationTest, SseResponseMultipleEventsPassThrough) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "id": 3,
    "params": {
      "name": "time__long_running_task",
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

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Backend responds with SSE containing multiple events (progress + final result)
  const std::string sse_response =
      "data: "
      "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"progress\",\"params\":{\"progress\":50}}\n\n"
      "data: "
      "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":[{\"type\":\"text\",\"text\":"
      "\"completed\"}]}}\n\n";

  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl response_body(sse_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The final response should be SSE with the actual result
  EXPECT_EQ("text/event-stream", response->headers().getContentTypeValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("completed"));
}

// Test resources/list request fans out to both backends and aggregates resources.
TEST_P(McpRouterIntegrationTest, ResourcesListFanoutAggregation) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "resources/list",
    "id": 20
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

  // Time backend returns a resource.
  const std::string time_response = R"({
    "jsonrpc": "2.0",
    "id": 20,
    "result": {
      "resources": [
        {"uri": "file://current_time", "name": "Current Time", "mimeType": "text/plain"}
      ]
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_body(time_response);
  time_backend_request_->encodeData(time_body, true);

  // Tools backend returns resources.
  const std::string tools_response = R"({
    "jsonrpc": "2.0",
    "id": 20,
    "result": {
      "resources": [
        {"uri": "file://config", "name": "Config File", "description": "Configuration settings"},
        {"uri": "file://data", "name": "Data File"}
      ]
    }
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_body(tools_response);
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify the aggregated response contains resources from both backends with backend+scheme
  // prefixes.
  EXPECT_THAT(response->body(), testing::HasSubstr("time+file://current_time"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools+file://config"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools+file://data"));
}

// Test resources/list aggregates SSE responses from backends into JSON.
TEST_P(McpRouterIntegrationTest, ResourcesListAggregatesSseResponses) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "resources/list",
    "id": 25
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

  // Time backend responds with SSE.
  const std::string time_json =
      R"({"jsonrpc":"2.0","id":25,"result":{"resources":[{"uri":"file://current_time","name":"Current Time","mimeType":"text/plain"}]}})";
  const std::string time_sse = "data: " + time_json + "\n\n";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl time_body(time_sse);
  time_backend_request_->encodeData(time_body, true);

  // Tools backend responds with regular JSON.
  const std::string tools_response = R"({
    "jsonrpc": "2.0",
    "id": 25,
    "result": {
      "resources": [
        {"uri": "file://config", "name": "Config File", "description": "Configuration settings"},
        {"uri": "file://data", "name": "Data File"}
      ]
    }
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_body(tools_response);
  tools_backend_request_->encodeData(tools_body, true);

  // Wait for aggregated response.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Aggregated response should contain resources from both backends with correct URI prefixes.
  EXPECT_EQ("application/json", response->headers().getContentTypeValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("time+file://current_time"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools+file://config"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools+file://data"));
}

// Test resources/list aggregation when a backend sends SSE with intermediate notifications
// before the final response. The client should receive an SSE response with the notification
// forwarded and the aggregated result as the final event.
TEST_P(McpRouterIntegrationTest, ResourcesListSseWithIntermediateNotifications) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "resources/list",
    "id": 26
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

  // Time backend sends SSE with a notification event followed by the final response.
  const std::string notification_event =
      "data: "
      "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\","
      "\"params\":{\"progressToken\":\"abc\",\"progress\":50,\"total\":100}}\n\n";
  const std::string response_event =
      "data: "
      "{\"jsonrpc\":\"2.0\",\"id\":26,\"result\":{\"resources\":"
      "[{\"uri\":\"file://current_time\",\"name\":\"Current Time\"}]}}\n\n";
  const std::string time_sse = notification_event + response_event;

  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl time_body(time_sse);
  time_backend_request_->encodeData(time_body, true);

  // Tools backend responds with regular JSON.
  const std::string tools_response = R"({
    "jsonrpc": "2.0",
    "id": 26,
    "result": {
      "resources": [
        {"uri": "file://config", "name": "Config File"}
      ]
    }
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_body(tools_response);
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Because intermediate notifications were forwarded, response should be SSE.
  EXPECT_EQ("text/event-stream", response->headers().getContentTypeValue());
  // The notification should be forwarded to client.
  EXPECT_THAT(response->body(), testing::HasSubstr("notifications/progress"));
  // The aggregated result should contain resources from both backends.
  EXPECT_THAT(response->body(), testing::HasSubstr("time+file://current_time"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools+file://config"));
}

// Test resources/read routes to correct backend based on URI scheme.
TEST_P(McpRouterIntegrationTest, ResourcesReadRoutesToCorrectBackend) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "resources/read",
    "id": 21,
    "params": {
      "uri": "time+file://current_time"
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

  // Request should be routed to time backend based on "time+" prefix in URI.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify upstream request body has URI rewritten (backend prefix stripped).
  EXPECT_THAT(time_backend_request_->body().toString(), testing::HasSubstr("file://current_time"));
  EXPECT_THAT(time_backend_request_->body().toString(), testing::Not(testing::HasSubstr("time+")));

  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 21,
    "result": {
      "contents": [{"uri": "file://current_time", "mimeType": "text/plain", "text": "2024-01-15T10:30:00Z"}]
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("2024-01-15T10:30:00Z"));
}

// Test resources/subscribe routes to correct backend.
TEST_P(McpRouterIntegrationTest, ResourcesSubscribeRoutesToBackend) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "resources/subscribe",
    "id": 22,
    "params": {
      "uri": "tools+file://config"
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

  // Request should be routed to tools backend based on "tools+" prefix in URI.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  // Verify upstream request body has URI rewritten (backend prefix stripped).
  EXPECT_THAT(tools_backend_request_->body().toString(), testing::HasSubstr("file://config"));

  // Subscribe returns empty result per MCP spec.
  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 22,
    "result": {}
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  tools_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("\"result\""));
  EXPECT_THAT(response->body(), testing::HasSubstr("\"id\": 22"));
}

// Test resources/unsubscribe routes to correct backend.
TEST_P(McpRouterIntegrationTest, ResourcesUnsubscribeRoutesToBackend) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "resources/unsubscribe",
    "id": 23,
    "params": {
      "uri": "time+file://current_time"
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

  // Request should be routed to time backend based on "time+" prefix in URI.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify upstream request body has URI rewritten.
  EXPECT_THAT(time_backend_request_->body().toString(), testing::HasSubstr("file://current_time"));

  // Unsubscribe returns empty result per MCP spec.
  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 23,
    "result": {}
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("\"result\""));
  EXPECT_THAT(response->body(), testing::HasSubstr("\"id\": 23"));
}

// Test resources/read with unknown backend URI returns 400.
TEST_P(McpRouterIntegrationTest, ResourcesReadWithUnknownBackendReturns400) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "resources/read",
    "id": 24,
    "params": {
      "uri": "unknown+file://some_resource"
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

  // Unknown backend prefix should return 400 Bad Request.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test prompts/list request fans out to both backends and aggregates prompts.
TEST_P(McpRouterIntegrationTest, PromptsListFanoutAggregation) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "prompts/list",
    "id": 30
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

  // Time backend returns a prompt.
  const std::string time_response = R"({
    "jsonrpc": "2.0",
    "id": 30,
    "result": {
      "prompts": [
        {"name": "greeting", "description": "A friendly greeting prompt"}
      ]
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_body(time_response);
  time_backend_request_->encodeData(time_body, true);

  // Tools backend returns prompts.
  const std::string tools_response = R"({
    "jsonrpc": "2.0",
    "id": 30,
    "result": {
      "prompts": [
        {"name": "code_review", "description": "Review code for issues"},
        {"name": "summarize", "description": "Summarize text", "arguments": [{"name": "text", "required": true}]}
      ]
    }
  })";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_body(tools_response);
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify the aggregated response contains prompts from both backends with name prefixes.
  EXPECT_THAT(response->body(), testing::HasSubstr("time__greeting"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__code_review"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__summarize"));
}

// Test prompts/get routes to correct backend based on name prefix.
TEST_P(McpRouterIntegrationTest, PromptsGetRoutesToCorrectBackend) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "prompts/get",
    "id": 31,
    "params": {
      "name": "time__greeting"
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

  // Request should be routed to time backend based on "time__" prefix.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify upstream request body has prompt name rewritten (prefix stripped).
  EXPECT_THAT(time_backend_request_->body().toString(), testing::HasSubstr("\"greeting\""));
  EXPECT_THAT(time_backend_request_->body().toString(), testing::Not(testing::HasSubstr("time__")));

  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 31,
    "result": {
      "description": "A friendly greeting prompt",
      "messages": [{"role": "user", "content": {"type": "text", "text": "Hello!"}}]
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("A friendly greeting prompt"));
}

// Test prompts/get with unknown backend prefix returns 400.
TEST_P(McpRouterIntegrationTest, PromptsGetWithUnknownBackendReturns400) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "prompts/get",
    "id": 32,
    "params": {
      "name": "unknown__some_prompt"
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

  // Unknown backend prefix should return 400 Bad Request.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test completion/complete with ref/prompt routes to correct backend.
TEST_P(McpRouterIntegrationTest, CompletionCompleteWithPromptRef) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "completion/complete",
    "id": 40,
    "params": {
      "ref": {
        "type": "ref/prompt",
        "name": "time__greeting"
      },
      "argument": {
        "name": "prefix",
        "value": "hel"
      }
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

  // Request should be routed to time backend based on "time__" prefix in prompt name.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify upstream request body has prompt name rewritten (prefix stripped).
  EXPECT_THAT(time_backend_request_->body().toString(), testing::HasSubstr("\"greeting\""));
  EXPECT_THAT(time_backend_request_->body().toString(), testing::Not(testing::HasSubstr("time__")));

  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 40,
    "result": {
      "completion": {
        "values": ["hello", "help", "helicopter"],
        "hasMore": false
      }
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("\"values\""));
  EXPECT_THAT(response->body(), testing::HasSubstr("hello"));
}

// Test completion/complete with ref/resource routes to correct backend.
TEST_P(McpRouterIntegrationTest, CompletionCompleteWithResourceRef) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "completion/complete",
    "id": 41,
    "params": {
      "ref": {
        "type": "ref/resource",
        "uri": "time+file://current_time"
      },
      "argument": {
        "name": "format",
        "value": "YYYY"
      }
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

  // Request should be routed to time backend based on "time+" prefix in resource URI.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  // Verify upstream request body has URI rewritten (backend prefix stripped).
  EXPECT_THAT(time_backend_request_->body().toString(),
              testing::HasSubstr("\"file://current_time\""));
  EXPECT_THAT(time_backend_request_->body().toString(), testing::Not(testing::HasSubstr("time+")));

  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 41,
    "result": {
      "completion": {
        "values": ["YYYY-MM-DD", "YYYY/MM/DD"],
        "hasMore": false
      }
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl response_body(backend_response);
  time_backend_request_->encodeData(response_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("\"values\""));
  EXPECT_THAT(response->body(), testing::HasSubstr("YYYY-MM-DD"));
}

// Test completion/complete with invalid ref type returns 400.
TEST_P(McpRouterIntegrationTest, CompletionCompleteWithInvalidRefType) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "completion/complete",
    "id": 42,
    "params": {
      "ref": {
        "type": "ref/invalid",
        "name": "something"
      },
      "argument": {
        "name": "prefix",
        "value": "test"
      }
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

  // Invalid ref type should return 400 Bad Request.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test logging/setLevel is forwarded to all backends.
TEST_P(McpRouterIntegrationTest, LoggingSetLevelFanout) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "logging/setLevel",
    "id": 50,
    "params": {
      "level": "debug"
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

  // Both backends should receive the request.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));

  // Wait for both to receive the full request.
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  // Verify both backends received the logging level parameter.
  EXPECT_THAT(time_backend_request_->body().toString(), testing::HasSubstr("\"level\""));
  EXPECT_THAT(tools_backend_request_->body().toString(), testing::HasSubstr("\"level\""));

  // Backends respond with empty result.
  const std::string backend_response = R"({"jsonrpc":"2.0","id":50,"result":{}})";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_resp(backend_response);
  time_backend_request_->encodeData(time_resp, true);

  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_resp(backend_response);
  tools_backend_request_->encodeData(tools_resp, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Response should be a JSON-RPC result with empty object.
  EXPECT_THAT(response->body(), testing::HasSubstr("\"result\""));
}

// Test notifications/cancelled is forwarded to all backends.
TEST_P(McpRouterIntegrationTest, NotificationCancelledFanout) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Notifications don't have an 'id' field per JSON-RPC spec.
  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "notifications/cancelled",
    "params": {
      "requestId": "req-123",
      "reason": "User cancelled"
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

  // Both backends should receive the notification.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  // Backends respond with 202 Accepted (notifications don't return content).
  time_backend_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);
  tools_backend_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("202", response->headers().getStatusValue());
}

// Test notifications/roots/list_changed is forwarded to all backends.
TEST_P(McpRouterIntegrationTest, NotificationRootsListChangedFanout) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "notifications/roots/list_changed"
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

  // Both backends should receive the notification.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  time_backend_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);
  tools_backend_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("202", response->headers().getStatusValue());
}

class McpRouterSubjectValidationIntegrationTest : public McpRouterIntegrationTest {
public:
  void initializeFilterWithSubjectValidation() {
    config_helper_.skipPortUsageValidation();

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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
    });

    // MCP router with session identity and ENFORCE validation
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
        session_identity:
          identity:
            header:
              name: x-user-id
          validation:
            mode: ENFORCE
    )EOF");

    config_helper_.prependFilter(R"EOF(
      name: envoy.filters.http.mcp
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
        traffic_mode: PASS_THROUGH
    )EOF");

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

    HttpIntegrationTest::initialize();
  }

  std::string encodeSessionId(const std::string& route, const std::string& subject,
                              const std::string& backend_session) {
    std::string backend_encoded = Base64::encode(backend_session.data(), backend_session.size());
    std::string composite = route + "@" + subject + "@time:" + backend_encoded;
    return Base64::encode(composite.data(), composite.size());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, McpRouterSubjectValidationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Subject mismatch returns 403
TEST_P(McpRouterSubjectValidationIntegrationTest, SubjectMismatchReturns403) {
  initializeFilterWithSubjectValidation();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Session ID has subject "alice", but header says "bob"
  std::string session_id = encodeSessionId("test_route", "alice", "backend-session-123");

  const std::string request_body = R"({"jsonrpc":"2.0","method":"tools/list","id":1})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"mcp-session-id", session_id},
                                     {"x-user-id", "bob"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("403", response->headers().getStatusValue());

  // Verify stats: auth failure (subject mismatch)
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_total", 1);
  test_server_->waitForCounterEq("http.config_test.mcp_router.rq_auth_failure", 1);
}

// Missing auth header returns 403
TEST_P(McpRouterSubjectValidationIntegrationTest, MissingAuthHeaderReturns403) {
  initializeFilterWithSubjectValidation();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::string session_id = encodeSessionId("test_route", "alice", "backend-session-123");

  const std::string request_body = R"({"jsonrpc":"2.0","method":"tools/list","id":1})";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"mcp-session-id", session_id}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Test tools/list with SSE responses containing intermediate events (notifications, server
// requests) before the final response. Verifies intermediate events are classified and response is
// aggregated.
TEST_P(McpRouterIntegrationTest, ToolsListWithIntermediateSseEvents) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/list",
    "id": 100
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

  // Time backend responds with SSE containing: notification -> response.
  // The notification should be classified as intermediate event.
  const std::string time_sse =
      "data: "
      "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{\"progress\":50}}\n\n"
      "data: "
      "{\"jsonrpc\":\"2.0\",\"id\":100,\"result\":{\"tools\":[{\"name\":\"get_time\","
      "\"description\":\"Get current time\"}]}}\n\n";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl time_body(time_sse);
  time_backend_request_->encodeData(time_body, true);

  // Tools backend responds with SSE containing: server request -> notification -> response.
  const std::string tools_sse =
      "data: {\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"roots/list\",\"params\":{}}\n\n"
      "data: "
      "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/"
      "message\",\"params\":{\"level\":\"info\"}}\n\n"
      "data: "
      "{\"jsonrpc\":\"2.0\",\"id\":100,\"result\":{\"tools\":[{\"name\":\"calculator\","
      "\"description\":\"Math ops\"}]}}\n\n";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl tools_body(tools_sse);
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Aggregated response should contain tools from both backends with prefixes.
  EXPECT_THAT(response->body(), testing::HasSubstr("time__get_time"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__calculator"));
}

// Test tools/list with SSE containing server-to-client requests (roots/list) that should be
// classified as ServerRequest type and intermediate events are handled correctly.
TEST_P(McpRouterIntegrationTest, ToolsListSseWithServerToClientRequests) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/list",
    "id": 200
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

  // Backend 1: Direct response (JSON, not SSE).
  const std::string time_response = R"({
    "jsonrpc": "2.0",
    "id": 200,
    "result": {
      "tools": [{"name": "clock", "description": "Show clock"}]
    }
  })";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_body(time_response);
  time_backend_request_->encodeData(time_body, true);

  // Backend 2: SSE with sampling/createMessage server request before response.
  const std::string tools_sse = "data: "
                                "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"sampling/"
                                "createMessage\",\"params\":{\"max_tokens\":100}}\n\n"
                                "data: "
                                "{\"jsonrpc\":\"2.0\",\"id\":200,\"result\":{\"tools\":[{\"name\":"
                                "\"ai_assist\",\"description\":\"AI assistance\"}]}}\n\n";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl tools_body(tools_sse);
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Aggregated response should contain tools from both backends.
  EXPECT_THAT(response->body(), testing::HasSubstr("time__clock"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__ai_assist"));
}

// Test that tools/list with intermediate SSE events results in SSE response format.
// Verifies: SSE headers sent to client, intermediate events forwarded, aggregated response as SSE.
TEST_P(McpRouterIntegrationTest, ToolsListSseStreamingWithIntermediateEvents) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/list",
    "id": 300
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

  // Time backend: SSE with notification (triggers SSE streaming mode) then response.
  const std::string time_sse =
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\","
      "\"params\":{\"progressToken\":\"t1\",\"progress\":50}}\n\n"
      "data: {\"jsonrpc\":\"2.0\",\"id\":300,\"result\":{\"tools\":[{\"name\":\"timer\"}]}}\n\n";
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  Buffer::OwnedImpl time_body(time_sse);
  time_backend_request_->encodeData(time_body, true);

  // Tools backend: Direct JSON response (no SSE).
  const std::string tools_json =
      R"({"jsonrpc":"2.0","id":300,"result":{"tools":[{"name":"calc"}]}})";
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_body(tools_json);
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Since intermediate SSE event was received, response should be SSE format.
  EXPECT_EQ("text/event-stream", response->headers().getContentTypeValue());

  // Body should contain SSE events: forwarded notification + aggregated response.
  // Check for notification event (forwarded intermediate event).
  EXPECT_THAT(response->body(), testing::HasSubstr("event: message"));
  EXPECT_THAT(response->body(), testing::HasSubstr("notifications/progress"));
  // Check for aggregated tools in final response.
  EXPECT_THAT(response->body(), testing::HasSubstr("time__timer"));
  EXPECT_THAT(response->body(), testing::HasSubstr("tools__calc"));
}

// Test initialize with mixed session modes: one backend returns mcp-session-id, the other doesn't.
// The composite session encodes only the stateful backend. On a subsequent tools/list, the
// stateful backend gets its session header while the stateless backend gets none.
TEST_P(McpRouterIntegrationTest, InitializeMixedSessionAndSessionless) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

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

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  // Time backend returns WITH mcp-session-id.
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"mcp-session-id", "time-session-mixed"}},
      false);
  Buffer::OwnedImpl time_body(
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"time","version":"1.0"},"capabilities":{}}})");
  time_backend_request_->encodeData(time_body, true);

  // Tools backend returns WITHOUT mcp-session-id.
  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_body(
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"tools","version":"1.0"},"capabilities":{}}})");
  tools_backend_request_->encodeData(tools_body, true);

  ASSERT_TRUE(init_response->waitForEndStream());
  EXPECT_EQ("200", init_response->headers().getStatusValue());

  // Composite session should exist (at least one backend returned a session).
  auto session_header = init_response->headers().get(Http::LowerCaseString("mcp-session-id"));
  ASSERT_FALSE(session_header.empty());
  std::string composite_session = std::string(session_header[0]->value().getStringView());

  // Decode and verify only the time backend is in the composite.
  std::string decoded_session = Base64::decode(composite_session);
  EXPECT_FALSE(decoded_session.empty());
  std::string time_session_base64 =
      Base64::encode("time-session-mixed", strlen("time-session-mixed"));
  EXPECT_THAT(decoded_session, testing::HasSubstr("time:" + time_session_base64));
  // tools backend should NOT appear in the composite session.
  EXPECT_THAT(decoded_session, testing::Not(testing::HasSubstr("tools:")));

  // Subsequent tools/list using the composite session.
  // This verifies that backend_sessions_[backend.name] returns empty for "tools" (not in map),
  // and createUpstreamHeaders handles that correctly by not sending mcp-session-id to that backend.
  const std::string list_body = R"({"jsonrpc":"2.0","method":"tools/list","id":2})";

  FakeStreamPtr time_backend_request2;
  FakeStreamPtr tools_backend_request2;
  auto list_response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"},
                                     {"mcp-session-id", composite_session}},
      list_body);

  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request2));
  ASSERT_TRUE(time_backend_request2->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request2));
  ASSERT_TRUE(tools_backend_request2->waitForEndStream(*dispatcher_));

  // Time backend should receive mcp-session-id (it had a session).
  auto time_upstream_session =
      time_backend_request2->headers().get(Http::LowerCaseString("mcp-session-id"));
  ASSERT_FALSE(time_upstream_session.empty());
  EXPECT_EQ("time-session-mixed", time_upstream_session[0]->value().getStringView());

  // Tools backend should NOT receive mcp-session-id (it was session-less).
  auto tools_upstream_session =
      tools_backend_request2->headers().get(Http::LowerCaseString("mcp-session-id"));
  EXPECT_TRUE(tools_upstream_session.empty());

  // Both respond successfully.
  time_backend_request2->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_list_body(
      R"({"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"get_time","description":"Time"}]}})");
  time_backend_request2->encodeData(time_list_body, true);

  tools_backend_request2->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_list_body(
      R"({"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"calc","description":"Calculator"}]}})");
  tools_backend_request2->encodeData(tools_list_body, true);

  ASSERT_TRUE(list_response->waitForEndStream());
  EXPECT_EQ("200", list_response->headers().getStatusValue());

  // Aggregated tools should contain both backends' tools.
  EXPECT_THAT(list_response->body(), testing::HasSubstr("time__get_time"));
  EXPECT_THAT(list_response->body(), testing::HasSubstr("tools__calc"));
}

// Test full session-less flow end-to-end: initialize where no backends return mcp-session-id,
// then a subsequent tools/list without any session header. Verifies:
// 1. Initialize succeeds with no mcp-session-id returned to client.
// 2. decodeAndParseSession is skipped (encoded_session_id_ empty), backend_sessions_ stays empty.
// 3. createUpstreamHeaders omits mcp-session-id for both backends.
// 4. Fanout aggregation still works correctly.
TEST_P(McpRouterIntegrationTest, InitializeWithoutSessionIdsAndSubsequentToolsList) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Step 1: Initialize — both backends respond without mcp-session-id.
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

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, time_backend_connection_));
  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request_));
  ASSERT_TRUE(time_backend_request_->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, tools_backend_connection_));
  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request_));
  ASSERT_TRUE(tools_backend_request_->waitForEndStream(*dispatcher_));

  // Both backends respond WITHOUT mcp-session-id.
  time_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_init_body(
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"time","version":"1.0"},"capabilities":{"tools":{}}}})");
  time_backend_request_->encodeData(time_init_body, true);

  tools_backend_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_init_body(
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"tools","version":"1.0"},"capabilities":{"tools":{}}}})");
  tools_backend_request_->encodeData(tools_init_body, true);

  ASSERT_TRUE(init_response->waitForEndStream());
  EXPECT_EQ("200", init_response->headers().getStatusValue());

  // Verify response does NOT have mcp-session-id header when no backends returned one.
  auto session_header = init_response->headers().get(Http::LowerCaseString("mcp-session-id"));
  EXPECT_TRUE(session_header.empty());

  // Verify the response body contains gateway capabilities.
  EXPECT_THAT(init_response->body(), testing::HasSubstr("protocolVersion"));
  EXPECT_THAT(init_response->body(), testing::HasSubstr("envoy-mcp-gateway"));

  // Step 2: Subsequent tools/list without any mcp-session-id header.
  // Since no session was returned during initialize, the client sends no session.
  const std::string list_body = R"({"jsonrpc":"2.0","method":"tools/list","id":10})";

  FakeStreamPtr time_backend_request2;
  FakeStreamPtr tools_backend_request2;
  auto list_response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "application/json"},
                                     {"accept", "text/event-stream"},
                                     {"content-type", "application/json"}},
      list_body);

  ASSERT_TRUE(time_backend_connection_->waitForNewStream(*dispatcher_, time_backend_request2));
  ASSERT_TRUE(time_backend_request2->waitForEndStream(*dispatcher_));

  ASSERT_TRUE(tools_backend_connection_->waitForNewStream(*dispatcher_, tools_backend_request2));
  ASSERT_TRUE(tools_backend_request2->waitForEndStream(*dispatcher_));

  // Neither backend should receive mcp-session-id (backend_sessions_ is empty).
  auto time_session = time_backend_request2->headers().get(Http::LowerCaseString("mcp-session-id"));
  EXPECT_TRUE(time_session.empty());

  auto tools_session =
      tools_backend_request2->headers().get(Http::LowerCaseString("mcp-session-id"));
  EXPECT_TRUE(tools_session.empty());

  // Both backends respond successfully.
  time_backend_request2->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl time_list_body(
      R"({"jsonrpc":"2.0","id":10,"result":{"tools":[{"name":"get_time","description":"Time"}]}})");
  time_backend_request2->encodeData(time_list_body, true);

  tools_backend_request2->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl tools_list_body(
      R"({"jsonrpc":"2.0","id":10,"result":{"tools":[{"name":"calc","description":"Calc"}]}})");
  tools_backend_request2->encodeData(tools_list_body, true);

  ASSERT_TRUE(list_response->waitForEndStream());
  EXPECT_EQ("200", list_response->headers().getStatusValue());

  // Response should not have mcp-session-id since no session context exists.
  auto response_session = list_response->headers().get(Http::LowerCaseString("mcp-session-id"));
  EXPECT_TRUE(response_session.empty());

  // Aggregated tools from both backends should be present.
  EXPECT_THAT(list_response->body(), testing::HasSubstr("time__get_time"));
  EXPECT_THAT(list_response->body(), testing::HasSubstr("tools__calc"));
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
