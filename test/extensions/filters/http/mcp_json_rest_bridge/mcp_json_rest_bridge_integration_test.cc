#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/http/codec.h"
#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/integration/fake_access_log.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
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

TEST_P(McpJsonRestBridgeIntegrationTest, MethodNotAllowed) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/mcp"}, {":scheme", "http"}, {":authority", "host"}});

  // Since the filter calls sendLocalReply and stops iteration early in decodeHeaders,
  // the stream might be reset rather than ending cleanly. We use waitForAnyTermination
  // to ensure we wait for the response headers regardless of how the stream terminates.
  ASSERT_TRUE(response->waitForAnyTermination());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("405"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, InvalidJsonBody) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({ "invalid": json })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("400"));
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"JSON parse error"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, MissingMethod) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 1
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":1,"error":{"code":-32600,"message":"Missing method field"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, MethodFieldNotString) {
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
    "method": 123
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":1,"error":{"code":-32600,"message":"Method field is not a string"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallInvalidParams) {
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
    "method": "tools/call",
    "params": "not_an_object"
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":1,"error":{"code":-32602,"message":"Invalid params"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallNonStringToolName) {
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
    "method": "tools/call",
    "params": {
      "name": 123
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":1,"error":{"code":-32602,"message":"Tool name not found"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallNonObjectArguments) {
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

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "method": "tools/call",
    "params": {
      "name": "create_api_key",
      "arguments": "invalid_arguments"
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":321,"error":{"code":-32602,"message":"Tool arguments must be an object"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, UnsupportedMcpProtocolVersionHeader) {
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
    "method": "tools/list"
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"},
                                     {"mcp-protocol-version", "1999-01-01"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":1,"error":{"code":-32602,"message":"Unsupported protocol version"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, MissingIdField) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "tools/list"
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Missing ID field"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, UnsupportedMethod) {
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
    "method": "tools/update"
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"Method tools/update is not supported"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, InitializeMissingProtocolVersion) {
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":1,"error":{"code":-32602,"message":"Missing valid protocolVersion in initialize request"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, UnknownTool) {
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

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "method": "tools/call",
    "params": {
      "name": "unknown_tool",
      "arguments": {}
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
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":321,"error":{"code":-32602,"message":"Unknown tool"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, InvalidArguments) {
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

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "method": "tools/call",
    "params": {
      "name": "create_api_key",
      "arguments": {
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

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":321,"error":{"code":-32602,"message":"Invalid tool arguments"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallWithErrorResponse) {
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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(500);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add(R"({"error": "Internal Server Error"})");
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "{\"error\": \"Internal Server Error\"}"
        }
      ],
      "isError": true
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallTranscoding) {
  FakeAccessLogFactory factory;
  Registry::InjectFactory<AccessLog::AccessLogInstanceFactory> factory_register(factory);

  bool metadata_verified = false;
  factory.setLogCallback(
      [&metadata_verified](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
        const auto& dynamic_metadata = stream_info.dynamicMetadata().filter_metadata();
        auto it = dynamic_metadata.find("envoy.filters.http.mcp_json_rest_bridge");
        if (it != dynamic_metadata.end()) {
          Protobuf::Struct expected_metadata;
          MessageUtil::loadFromJson(R"json({
            "status": "mcp_json_rest_bridge_ok",
            "method": "tools/call",
            "backend_response_code": 200,
            "params": {
              "name": "create_api_key",
              "arguments": {
                "parent": "projects/foo",
                "key": {
                  "displayName": "bar"
                }
              }
            }
          })json",
                                    expected_metadata);
          EXPECT_TRUE(TestUtility::protoEqual(expected_metadata, it->second))
              << "got:\n"
              << it->second.DebugString() << "\nexpected:\n"
              << expected_metadata.DebugString();
          metadata_verified = true;
        }
      });

  config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
    auto* access_log = hcm.add_access_log();
    access_log->set_name("envoy.access_loggers.test");
    test::integration::accesslog::FakeAccessLog access_log_config;
    std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
  });

  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      request_storage_mode: DYNAMIC_METADATA
      tool_config:
        tools:
          - name: "create_api_key"
            http_rule:
              post: "/v1/{parent=projects/*}/keys"
              body: "key"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

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
  EXPECT_TRUE(metadata_verified);
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallWithTrailersTranscoding) {
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

  // Send headers without end_stream.
  upstream_request_->encodeHeaders(response_headers, false);

  // Send body without end_stream.
  Buffer::OwnedImpl response_data;
  response_data.add(R"({"displayName":"bar","createTime":"1970-01-01T00:00:22Z"})");
  upstream_request_->encodeData(response_data, false);

  // Send trailers to end stream.
  Http::TestResponseTrailerMapImpl response_trailers;
  upstream_request_->encodeTrailers(response_trailers);

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

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallRequestBodyExceedsLimit) {
  FakeAccessLogFactory factory;
  Registry::InjectFactory<AccessLog::AccessLogInstanceFactory> factory_register(factory);

  bool metadata_verified = false;
  factory.setLogCallback(
      [&metadata_verified](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
        const auto& dynamic_metadata = stream_info.dynamicMetadata().filter_metadata();
        auto it = dynamic_metadata.find("envoy.filters.http.mcp_json_rest_bridge");
        if (it != dynamic_metadata.end()) {
          Protobuf::Struct expected_metadata;
          MessageUtil::loadFromJson(R"json({
            "status": "mcp_json_rest_bridge_request_too_large"
          })json",
                                    expected_metadata);
          EXPECT_TRUE(TestUtility::protoEqual(expected_metadata, it->second))
              << "got:\n"
              << it->second.DebugString() << "\nexpected:\n"
              << expected_metadata.DebugString();
          metadata_verified = true;
        }
      });

  config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
    auto* access_log = hcm.add_access_log();
    access_log->set_name("envoy.access_loggers.test");
    test::integration::accesslog::FakeAccessLog access_log_config;
    std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
  });

  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      max_request_body_size: 10
      request_storage_mode: DYNAMIC_METADATA
      tool_config:
        tools:
          - name: "create_api_key"
            http_rule:
              post: "/v1/{parent=projects/*}/keys"
              body: "key"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

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

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("413"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":null,"error":{"code":-32000,"message":"Request body too large"}})json"));
  EXPECT_TRUE(metadata_verified);
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallResponseBodyExceedsLimit) {
  FakeAccessLogFactory factory;
  Registry::InjectFactory<AccessLog::AccessLogInstanceFactory> factory_register(factory);

  bool metadata_verified = false;
  factory.setLogCallback(
      [&metadata_verified](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
        const auto& dynamic_metadata = stream_info.dynamicMetadata().filter_metadata();
        auto it = dynamic_metadata.find("envoy.filters.http.mcp_json_rest_bridge");
        if (it != dynamic_metadata.end()) {
          Protobuf::Struct expected_metadata;
          MessageUtil::loadFromJson(R"json({
            "status": "mcp_json_rest_bridge_response_too_large",
            "backend_response_code": 200,
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
          })json",
                                    expected_metadata);
          EXPECT_TRUE(TestUtility::protoEqual(expected_metadata, it->second))
              << "got:\n"
              << it->second.DebugString() << "\nexpected:\n"
              << expected_metadata.DebugString();
          metadata_verified = true;
        }
      });

  config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
    auto* access_log = hcm.add_access_log();
    access_log->set_name("envoy.access_loggers.test");
    test::integration::accesslog::FakeAccessLog access_log_config;
    std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
  });

  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      max_response_body_size: 10
      request_storage_mode: DYNAMIC_METADATA
      tool_config:
        tools:
          - name: "create_api_key"
            http_rule:
              post: "/v1/{parent=projects/*}/keys"
              body: "key"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add("12345678901"); // 11 bytes, exceeds 10
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));
  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":321,"error":{"code":-32000,"message":"Response body too large"}})json"));
  EXPECT_TRUE(metadata_verified);
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

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallStreamingTranscoding) {
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
            text_content_streaming_enabled: true
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
  response_headers.setContentLength(21);
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl chunk1;
  chunk1.add(R"({"displayName":)");
  upstream_request_->encodeData(chunk1, false);

  Buffer::OwnedImpl chunk2;
  chunk2.add(R"("bar"})");
  upstream_request_->encodeData(chunk2, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());

  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(), IsEmpty());

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "{\"displayName\":\"bar\"}"
        }
      ],
      "isError": false
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallStreamingWithTrailers) {
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
            text_content_streaming_enabled: true
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl chunk1;
  chunk1.add(R"({"displayName":)");
  upstream_request_->encodeData(chunk1, false);

  Buffer::OwnedImpl chunk2;
  chunk2.add(R"("bar"})");
  upstream_request_->encodeData(chunk2, false);

  Http::TestResponseTrailerMapImpl response_trailers;
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());

  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(), IsEmpty());

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 321,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "{\"displayName\":\"bar\"}"
        }
      ],
      "isError": false
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsListLocalResponse) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
  )EOF";

  config_helper_.addConfigModifier([](envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) {
    auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute per_route;
    auto* tool_config = per_route.add_tool_config();
    tool_config->mutable_tool_list_local();
    auto* tool = tool_config->add_tools();
    tool->set_name("my_local_tool");
    tool->mutable_tool_list_config()->set_title("My Local Tool");
    tool->mutable_tool_list_config()->set_description("Does a local thing.");
    tool->mutable_tool_list_config()->set_input_schema(R"({"type":"object"})");

    Protobuf::Any per_route_any;
    MessageUtil::packFrom(per_route_any, per_route);
    route->mutable_typed_per_filter_config()->insert(
        {"envoy.filters.http.mcp_json_rest_bridge", per_route_any});
  });

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

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 123,
    "result": {
      "tools": [
        {
          "name": "my_local_tool",
          "title": "My Local Tool",
          "description": "Does a local thing.",
          "inputSchema": {
            "type": "object"
          }
        }
      ]
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, PerRouteConfigOverridesHttpRule) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      tool_config:
        tools:
          - name: "create_api_key"
            http_rule:
              post: "/v1/{parent=projects/*}/keys_default"
              body: "key"
  )EOF";

  config_helper_.addConfigModifier([](envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) {
    auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute per_route;
    auto* tool = per_route.add_tool_config()->add_tools();
    tool->set_name("create_api_key");
    tool->mutable_http_rule()->set_post("/v1/{parent=projects/*}/keys_override");
    tool->mutable_http_rule()->set_body("key");

    std::ignore =
        (*route->mutable_typed_per_filter_config())["envoy.filters.http.mcp_json_rest_bridge"]
            .PackFrom(per_route);
  });

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

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
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/v1/projects/foo/keys_override"));
  EXPECT_THAT(upstream_request_->headers().getContentTypeValue(), StrEq("application/json"));

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add(R"({"displayName":"bar"})");
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
          "text": "{\"displayName\":\"bar\"}"
        }
      ],
      "isError": false
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, PerRouteConfigWithCustomPath) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      tool_config:
        tools:
          - name: "create_api_key"
            http_rule:
              post: "/v1/{parent=projects/*}/keys_default"
              body: "key"
  )EOF";

  config_helper_.addConfigModifier([](envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) {
    auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute per_route;

    // Configure default_server_info with a custom path of "/custom_mcp"
    auto* tool_config = per_route.add_tool_config();
    tool_config->mutable_default_server_info()->set_path("/custom_mcp");
    tool_config->mutable_default_server_info()->set_host("host");

    auto* tool = tool_config->add_tools();
    tool->set_name("create_api_key");
    tool->mutable_http_rule()->set_post("/v1/{parent=projects/*}/keys_override");
    tool->mutable_http_rule()->set_body("key");

    std::ignore =
        (*route->mutable_typed_per_filter_config())["envoy.filters.http.mcp_json_rest_bridge"]
            .PackFrom(per_route);
  });

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // 1. Send request to custom path "/custom_mcp". It should match the custom path and get
  // transcoded.
  {
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
                                       {":path", "/custom_mcp"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"}},
        request_body);

    waitForNextUpstreamRequest();

    EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
    EXPECT_THAT(upstream_request_->headers().getPathValue(),
                StrEq("/v1/projects/foo/keys_override"));
    EXPECT_THAT(upstream_request_->headers().getContentTypeValue(), StrEq("application/json"));

    Http::TestResponseHeaderMapImpl response_headers;
    response_headers.setStatus(200);
    response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
    upstream_request_->encodeHeaders(response_headers, false);

    Buffer::OwnedImpl response_data;
    response_data.add(R"({"displayName":"bar"})");
    upstream_request_->encodeData(response_data, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(upstream_request_->complete());

    EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
    EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));

    const std::string expected_rpc_response = R"({
      "jsonrpc": "2.0",
      "id": 321,
      "result": {
        "content": [
          {
            "type": "text",
            "text": "{\"displayName\":\"bar\"}"
          }
        ],
        "isError": false
      }
    })";
    EXPECT_EQ(nlohmann::json::parse(response->body()),
              nlohmann::json::parse(expected_rpc_response));
  }

  // 2. Send request to unconfigured path "/unconfigured_mcp". It should bypass the bridge
  // completely and pass through to upstream as-is.
  {
    const std::string passthrough_body = R"({"some_passthrough_json": true})";

    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/unconfigured_mcp"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"}},
        passthrough_body);

    waitForNextUpstreamRequest();

    // Verify it arrived at the upstream unmodified
    EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
    EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/unconfigured_mcp"));
    EXPECT_THAT(upstream_request_->body().toString(), StrEq(passthrough_body));

    Http::TestResponseHeaderMapImpl response_headers;
    response_headers.setStatus(200);
    response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
    upstream_request_->encodeHeaders(response_headers, false);

    Buffer::OwnedImpl response_data;
    response_data.add(R"({"passthrough_response": true})");
    upstream_request_->encodeData(response_data, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(upstream_request_->complete());

    EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
    EXPECT_EQ(response->body(), R"({"passthrough_response": true})");
  }
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallWithTraceContextExtraction) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      trace_context_extraction: {}
      tool_config:
        tools:
          - name: "create_api_key"
            http_rule:
              post: "/v1/{parent=projects/*}/keys"
              body: "key"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

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
      },
      "_meta": {
        "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
        "tracestate": "key2=value2",
        "baggage": "key3=value3"
      }
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"},
                                     {"traceparent", "00-old-traceparent-00"},
                                     {"tracestate", "old-tracestate"},
                                     {"baggage", "old-baggage"}},
      request_body);

  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/v1/projects/foo/keys"));

  // Verify trace context headers from _meta are propagated to the upstream, replacing originals.
  auto traceparent = upstream_request_->headers().get(Http::LowerCaseString("traceparent"));
  ASSERT_EQ(traceparent.size(), 1);
  EXPECT_THAT(traceparent[0]->value().getStringView(),
              StrEq("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"));

  auto tracestate = upstream_request_->headers().get(Http::LowerCaseString("tracestate"));
  ASSERT_EQ(tracestate.size(), 1);
  EXPECT_THAT(tracestate[0]->value().getStringView(), StrEq("key2=value2"));

  auto baggage = upstream_request_->headers().get(Http::LowerCaseString("baggage"));
  ASSERT_EQ(baggage.size(), 1);
  EXPECT_THAT(baggage[0]->value().getStringView(), StrEq("key3=value3"));

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
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallTraceContextExtractionDisabledByDefault) {
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
      },
      "_meta": {
        "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
        "tracestate": "key2=value2",
        "baggage": "key3=value3"
      }
    }
  })";

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"},
                                     {"traceparent", "00-old-traceparent-00"},
                                     {"tracestate", "old-tracestate"},
                                     {"baggage", "old-baggage"}},
      request_body);

  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/v1/projects/foo/keys"));

  // Verify original trace headers are preserved when extraction is disabled.
  auto traceparent = upstream_request_->headers().get(Http::LowerCaseString("traceparent"));
  ASSERT_EQ(traceparent.size(), 1);
  EXPECT_THAT(traceparent[0]->value().getStringView(), StrEq("00-old-traceparent-00"));

  auto tracestate = upstream_request_->headers().get(Http::LowerCaseString("tracestate"));
  ASSERT_EQ(tracestate.size(), 1);
  EXPECT_THAT(tracestate[0]->value().getStringView(), StrEq("old-tracestate"));

  auto baggage = upstream_request_->headers().get(Http::LowerCaseString("baggage"));
  ASSERT_EQ(baggage.size(), 1);
  EXPECT_THAT(baggage[0]->value().getStringView(), StrEq("old-baggage"));

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
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallStreamingErrorResponse) {
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
            text_content_streaming_enabled: true
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 42,
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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(500);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add("Internal Server Error");
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());

  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(), IsEmpty());

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 42,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "Internal Server Error"
        }
      ],
      "isError": true
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, PerRouteOnlyNoOpModeIntegrationTest) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      per_route_only: true
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

  // Since the filter is in no-op mode (no fallback path registered at "/mcp"),
  // the request should be forwarded completely untouched to the upstream backend.
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/mcp"));
  EXPECT_THAT(upstream_request_->body().toString(), StrEq(request_body));

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add("raw backend response");
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->body(), StrEq("raw backend response"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, PerRouteOnlyWithExplicitPerRouteConfigWorks) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      per_route_only: true
  )EOF";

  // Override config_helper_ directly to add per-route config.
  config_helper_.addConfigModifier([](envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) {
    auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute per_route;
    auto* tool_config = per_route.add_tool_config();
    tool_config->mutable_default_server_info()->set_path("/explicit_mcp");
    auto* tool = tool_config->add_tools();
    tool->set_name("create_api_key");
    tool->mutable_http_rule()->set_post("/v1/{parent=projects/*}/keys");
    tool->mutable_http_rule()->set_body("key");

    Protobuf::Any per_route_any;
    MessageUtil::packFrom(per_route_any, per_route);
    route->mutable_typed_per_filter_config()->insert(
        {"envoy.filters.http.mcp_json_rest_bridge", per_route_any});
  });

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // First, verify that a request to "/mcp" goes through unmodified (no fallback).
  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2025-06-18",
      "capabilities": {}
    }
  })";

  auto response1 = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/mcp"));
  Http::TestResponseHeaderMapImpl response_headers1;
  response_headers1.setStatus(200);
  upstream_request_->encodeHeaders(response_headers1, false);
  Buffer::OwnedImpl response_data1("raw /mcp response");
  upstream_request_->encodeData(response_data1, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_THAT(response1->body(), StrEq("raw /mcp response"));

  // Now, verify that the explicit per-route config at "/explicit_mcp" is successfully intercepted.
  const std::string rpc_body = R"({
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

  auto response2 = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/explicit_mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      rpc_body);

  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("POST"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/v1/projects/foo/keys"));

  Http::TestResponseHeaderMapImpl response_headers2;
  response_headers2.setStatus(200);
  response_headers2.setContentType(Http::Headers::get().ContentTypeValues.Json);
  upstream_request_->encodeHeaders(response_headers2, false);
  Buffer::OwnedImpl response_data2(R"({"displayName":"bar","createTime":"1970-01-01T00:00:22Z"})");
  upstream_request_->encodeData(response_data2, true);

  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_THAT(response2->headers().getStatusValue(), StrEq("200"));
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
  EXPECT_EQ(nlohmann::json::parse(response2->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallHeadersOnly204SyntheticSuccessResult) {
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

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 7,
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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(204);
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 7,
    "result": {
      "content": [{"type": "text", "text": ""}],
      "isError": false
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallHeadersOnly5xxSyntheticErrorResult) {
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

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 8,
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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(503);
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 8,
    "result": {
      "content": [{"type": "text", "text": ""}],
      "isError": true
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallHeadersOnly304SyntheticSuccessResult) {
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

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 10,
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

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(304);
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 10,
    "result": {
      "content": [{"type": "text", "text": ""}],
      "isError": false
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsListHeadersOnly204SyntheticServerError) {
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
    "id": 9,
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
  response_headers.setStatus(204);
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  EXPECT_EQ(
      nlohmann::json::parse(response->body()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"Server error"}})json"));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallWithHeaderAndCookieBindings) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      tool_config:
        tools:
          - name: "list_api_keys"
            http_rule:
              get: "/v1/{parent=projects/*}/apiKeys"
              bindings:
                - type: HEADER
                  name: "x-api-key"
                  argument_path: "api_key"
                - type: HEADER
                  name: "x-request-id"
                  argument_path: "request_id"
                - type: COOKIE
                  name: "SESSION_ID"
                  argument_path: "session_id"
                - type: COOKIE
                  name: "PREF"
                  argument_path: "pref"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 456,
    "method": "tools/call",
    "params": {
      "name": "list_api_keys",
      "arguments": {
        "parent": "projects/my-project",
        "api_key": "key-123",
        "request_id": "req-abc",
        "session_id": "xyz",
        "pref": "dark",
        "page_size": 10
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

  // Verify HTTP method and path (bound params excluded from query).
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("GET"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(),
              StrEq("/v1/projects/my-project/apiKeys?page_size=10"));

  // Verify header bindings are forwarded to upstream.
  auto api_key_headers = upstream_request_->headers().get(Http::LowerCaseString("x-api-key"));
  ASSERT_EQ(api_key_headers.size(), 1);
  EXPECT_THAT(api_key_headers[0]->value().getStringView(), StrEq("key-123"));

  auto req_id_headers = upstream_request_->headers().get(Http::LowerCaseString("x-request-id"));
  ASSERT_EQ(req_id_headers.size(), 1);
  EXPECT_THAT(req_id_headers[0]->value().getStringView(), StrEq("req-abc"));

  // Verify cookie bindings are forwarded to upstream as a Cookie header.
  auto cookie_headers = upstream_request_->headers().get(Http::Headers::get().Cookie);
  ASSERT_EQ(cookie_headers.size(), 1);
  absl::string_view cookie_value = cookie_headers[0]->value().getStringView();
  // Cookie order is not deterministic (comes from flat_hash_map).
  EXPECT_TRUE(cookie_value == "SESSION_ID=xyz; PREF=dark" ||
              cookie_value == "PREF=dark; SESSION_ID=xyz")
      << "Actual cookie: " << cookie_value;

  // No request body for GET.
  EXPECT_THAT(upstream_request_->body().toString(), IsEmpty());

  // Send upstream response.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add(R"({"keys":[{"name":"projects/my-project/apiKeys/key-1"}]})");
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
  EXPECT_THAT(response->headers().getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response->headers().getContentLengthValue(),
              StrEq(std::to_string(response->body().size())));

  const std::string expected_rpc_response = R"({
    "jsonrpc": "2.0",
    "id": 456,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "{\"keys\":[{\"name\":\"projects/my-project/apiKeys/key-1\"}]}"
        }
      ],
      "isError": false
    }
  })";
  EXPECT_EQ(nlohmann::json::parse(response->body()), nlohmann::json::parse(expected_rpc_response));
}

TEST_P(McpJsonRestBridgeIntegrationTest, ToolsCallIgnoresRestrictedHeaders) {
  const std::string config = R"EOF(
    name: envoy.filters.http.mcp_json_rest_bridge
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
      tool_config:
        tools:
          - name: "test_tool"
            http_rule:
              get: "/v1/test"
              bindings:
                - type: HEADER
                  name: "host"
                  argument_path: "host_arg"
                - type: HEADER
                  name: "content-length"
                  argument_path: "cl_arg"
                - type: HEADER
                  name: "x-envoy-restricted"
                  argument_path: "x_envoy_arg"
                - type: HEADER
                  name: "x-allowed-header"
                  argument_path: "allowed_arg"
  )EOF";

  initializeFilter(config);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "id": 123,
    "method": "tools/call",
    "params": {
      "name": "test_tool",
      "arguments": {
        "host_arg": "evil.com",
        "cl_arg": "999",
        "x_envoy_arg": "malicious",
        "allowed_arg": "safe"
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

  // Verify HTTP method and path
  EXPECT_THAT(upstream_request_->headers().getMethodValue(), StrEq("GET"));
  EXPECT_THAT(upstream_request_->headers().getPathValue(), StrEq("/v1/test"));

  // Verify allowed header is forwarded
  auto allowed_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-allowed-header"));
  ASSERT_EQ(allowed_headers.size(), 1);
  EXPECT_THAT(allowed_headers[0]->value().getStringView(), StrEq("safe"));

  // Verify restricted headers are ignored
  EXPECT_THAT(upstream_request_->headers().getHostValue(), StrEq("host")); // Unchanged
  EXPECT_TRUE(
      upstream_request_->headers().get(Http::LowerCaseString("x-envoy-restricted")).empty());
  // content-length might be set or not depending on body, but shouldn't be "999"
  if (upstream_request_->headers().ContentLength()) {
    EXPECT_THAT(upstream_request_->headers().getContentLengthValue(), Not(StrEq("999")));
  }

  // Send upstream response.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add(R"({"status":"ok"})");
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_THAT(response->headers().getStatusValue(), StrEq("200"));
}

} // namespace
} // namespace Envoy
