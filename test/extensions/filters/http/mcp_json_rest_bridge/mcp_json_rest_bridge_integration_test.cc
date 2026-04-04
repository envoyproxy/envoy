#include "envoy/http/codec.h"
#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp" // IWYU pragma: keep

namespace Envoy {
namespace {

using ::testing::IsEmpty;
using ::testing::StrEq;

class McpJsonRestBridgeIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public HttpIntegrationTest {
public:
  McpJsonRestBridgeIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void SetUp() override { setUpstreamProtocol(Http::CodecType::HTTP2); }

  void initializeFilter(const std::string& config) {
    config_helper_.prependFilter(config);
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, McpJsonRestBridgeIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(McpJsonRestBridgeIntegrationTest, InitializeSuccess) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2025-06-18",
      "capabilities": {}
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  const std::string expected_response = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "capabilities": {
        "tools": {
          "listChanged": false
        }
      },
      "protocolVersion": "2025-06-18",
      "serverInfo": {
        "name": "host",
        "version": "1.0.0"
      }
    }
  })";

  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, InitializedSuccess) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

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
                                     {"content-type", "application/json"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("202"));
  EXPECT_THAT(response->headers().getContentTypeValue(), IsEmpty());
  EXPECT_THAT(response->headers().getContentLengthValue(), IsEmpty());
  EXPECT_THAT(response->body(), IsEmpty());
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallTranscoding) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      tool_config:
        tools:
          - name: "create_api_key"
            http_rule:
              post: "/v1/{parent=projects/*}/keys"
              body: "key"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // TODO(guoyilin42): Add a test for large body.
  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "method": "tools/call",
    "params": {
      "name": "create_api_key",
      "arguments": {
        "parent": "projects/foo",
        "key": {
          "displayName": "bar"
        }
      }
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/v1/projects/foo/keys"));
  EXPECT_THAT(upstream_request_->body().toString(), StrEq(R"({"displayName":"bar"})"));

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add(R"({"displayName":"bar","createTime":"1970-01-01T00:00:22Z"})");
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));
  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "{\"displayName\":\"bar\",\"createTime\":\"1970-01-01T00:00:22Z\"}"
        }
      ],
      "isError": false
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsListTranscoding) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      tool_config:
        tool_list_http_rule:
          get: "/v1/tools"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 123,
    "method": "tools/list"
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("GET"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/v1/tools"));

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  const std::string backend_response_body = R"({
    "tools": [
      {
        "annotations": {},
        "description": "Create an API key",
        "inputSchema": {
          "description": "Request message for CreateApiKey method.",
          "properties": {
            "key": {
              "description": "Message for an API key.",
              "properties": {
                "displayName": {
                  "description": "Optional. The display name of the key.",
                  "type": "string"
                }
              },
              "type": "object"
            },
            "parent": {
              "description": "The parent resource must have the format of \"project/*\".",
              "type": "string"
            }
          },
          "type": "object"
        },
        "name": "create_api_key"
      }
    ]
  })";
  response_data.add(backend_response_body);
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 123,
    "result": {
      "tools": [
        {
          "annotations": {},
          "description": "Create an API key",
          "inputSchema": {
            "description": "Request message for CreateApiKey method.",
            "properties": {
              "key": {
                "description": "Message for an API key.",
                "properties": {
                  "displayName": {
                    "description": "Optional. The display name of the key.",
                    "type": "string"
                  }
                },
                "type": "object"
              },
              "parent": {
                "description": "The parent resource must have the format of \"project/*\".",
                "type": "string"
              }
            },
            "type": "object"
          },
          "name": "create_api_key"
        }
      ]
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsListPassthrough) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 123,
    "method": "tools/list"
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/mcp"));

  // Verify that the original request body is passed through.
  EXPECT_THAT(upstream_request_->body().toString(),
              StrEq(R"({"id":123,"jsonrpc":"2.0","method":"tools/list"})"));

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
  const std::string backend_response_body = R"({
    "jsonrpc": "2.0",
    "id": 123,
    "result": {
      "tools": [
        {
          "name": "passthrough_tool"
        }
      ]
    }
  })";
  response_headers.setContentLength(backend_response_body.size());

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add(backend_response_body);
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(backend_response_body));
}

TEST_P(McpJsonRestBridgeIntegrationTest, InitializeUnsupportedProtocolVersionFallsBackToLatest) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 100,
    "method": "initialize",
    "params": {
      "protocolVersion": "unsupported-version"
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  const std::string expected_response = R"({
    "jsonrpc": "2.0",
    "id": 100,
    "result": {
      "capabilities": {
        "tools": {
          "listChanged": false
        }
      },
      "protocolVersion": "2025-11-25",
      "serverInfo": {
        "name": "host",
        "version": "1.0.0"
      }
    }
  })";

  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_response));
}

} // namespace
} // namespace Envoy
