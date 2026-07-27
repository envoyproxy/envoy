#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using testing::_;
using testing::Eq;
using testing::Return;

class EndpointKeyedFilteringTest : public testing::Test {
public:
  void setupRouteConfig(const std::string& yaml_config) {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute
        proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge filter_proto;
    filter_config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(filter_proto);
    per_route_config_ = std::make_unique<McpJsonRestBridgePerRouteConfig>(proto_config);

    recreateFilter();
  }

  void recreateFilter() {
    filter_ = std::make_unique<McpJsonRestBridgeFilter>(filter_config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);

    EXPECT_CALL(decoder_callbacks_, requestHeaders())
        .WillRepeatedly(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    EXPECT_CALL(encoder_callbacks_, responseHeaders())
        .WillRepeatedly(Return(Http::ResponseHeaderMapOptRef(response_headers_)));
    EXPECT_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
        .WillRepeatedly(Return(per_route_config_.get()));
  }

  McpJsonRestBridgeFilterConfigSharedPtr filter_config_;
  std::unique_ptr<McpJsonRestBridgePerRouteConfig> per_route_config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
};

TEST_F(EndpointKeyedFilteringTest, PrecedenceFourStepMatching) {
  // We configure 4 different ServerToolConfig blocks representing the 4 matching precedence tiers:
  // 1. Exact match (a.com, /mcp)
  // 2. Host match (a.com, "")
  // 3. Path match ("", /mcp)
  // 4. Global match ("", "")
  setupRouteConfig(R"yaml(
tool_config:
  - tools:
      - name: tool_exact
        http_rule:
          get: "/exact"
        tool_list_config:
          description: "Exact"
    default_server_info:
      host: "a.com"
      path: "/mcp"
    tool_list_local: {}
  - tools:
      - name: tool_host_only
        http_rule:
          get: "/host_only"
        tool_list_config:
          description: "Host Only"
    default_server_info:
      host: "a.com"
    tool_list_local: {}
  - tools:
      - name: tool_path_only
        http_rule:
          get: "/path_only"
        tool_list_config:
          description: "Path Only"
    default_server_info:
      path: "/mcp"
    tool_list_local: {}
  - tools:
      - name: tool_global
        http_rule:
          get: "/global"
        tool_list_config:
          description: "Global"
    tool_list_local: {}
)yaml");

  // Case 1: Exact match request (a.com, /mcp)
  // Should return tool_exact, tool_host_only, tool_path_only, and tool_global (via precedence
  // merging).
  {
    request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {":authority", "a.com"}};
    recreateFilter();
    filter_->decodeHeaders(request_headers_, false);

    EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
        .WillOnce(testing::Invoke([](Http::Code, absl::string_view body, auto&&, auto&&, auto&&) {
          auto resp = nlohmann::json::parse(body);
          auto tools = resp["result"]["tools"];
          EXPECT_EQ(tools.size(), 4);

          EXPECT_EQ(tools[0]["name"], "tool_exact");
          EXPECT_EQ(tools[1]["name"], "tool_host_only");
          EXPECT_EQ(tools[2]["name"], "tool_path_only");
          EXPECT_EQ(tools[3]["name"], "tool_global");
        }));

    Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":1,"method":"tools/list"})json");
    filter_->decodeData(body, true);
  }

  // Case 2: Host match fallback.
  // The filter intercepts requests on the "/mcp" path, so path-based matching
  // is performed on "/mcp" (either exact or wildcard).
  // Testing with a different host (b.com) ensures it falls back to matching only
  // path-specific and global configurations.
  {
    request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {":authority", "b.com"}};
    recreateFilter();
    filter_->decodeHeaders(request_headers_, false);

    EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
        .WillOnce(testing::Invoke([](Http::Code, absl::string_view body, auto&&, auto&&, auto&&) {
          auto resp = nlohmann::json::parse(body);
          auto tools = resp["result"]["tools"];
          EXPECT_EQ(tools.size(), 2);
          EXPECT_EQ(tools[0]["name"], "tool_path_only");
          EXPECT_EQ(tools[1]["name"], "tool_global");
        }));

    Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":2,"method":"tools/list"})json");
    filter_->decodeData(body, true);
  }
}

TEST_F(EndpointKeyedFilteringTest, ToolCallUsesPrecedenceMatching) {
  setupRouteConfig(R"yaml(
tool_config:
  - tools:
      - name: tool_name
        http_rule:
          get: "/exact"
    default_server_info:
      host: "a.com"
      path: "/mcp"
  - tools:
      - name: tool_name
        http_rule:
          get: "/global"
)yaml");

  // Exact match request (a.com, /mcp) should select /exact route
  {
    request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {":authority", "a.com"}};
    recreateFilter();
    filter_->decodeHeaders(request_headers_, false);

    EXPECT_CALL(decoder_callbacks_, requestHeaders())
        .WillRepeatedly(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    Buffer::OwnedImpl body(
        R"json({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"tool_name","arguments":{}}})json");
    filter_->decodeData(body, true);

    EXPECT_EQ(request_headers_.getPathValue(), "/exact");
  }

  // Global fallback request (b.com, /mcp) should select /global route
  {
    request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {":authority", "b.com"}};
    recreateFilter();
    filter_->decodeHeaders(request_headers_, false);

    EXPECT_CALL(decoder_callbacks_, requestHeaders())
        .WillRepeatedly(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    Buffer::OwnedImpl body(
        R"json({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"tool_name","arguments":{}}})json");
    filter_->decodeData(body, true);

    EXPECT_EQ(request_headers_.getPathValue(), "/global");
  }
}

TEST_F(EndpointKeyedFilteringTest, TextContentStreamingEnabledReturnsFalse) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge filter_proto;
  TestUtility::loadFromYaml(R"yaml(
tool_config:
  tools:
    - name: tool_no_stream
      http_rule:
        get: "/no_stream"
  default_server_info:
    host: "a.com"
    path: "/mcp"
)yaml",
                            filter_proto);

  auto filter_config = std::make_shared<McpJsonRestBridgeFilterConfig>(filter_proto);

  EXPECT_FALSE(filter_config->textContentStreamingEnabled("tool_no_stream", "a.com", "/mcp"));
  EXPECT_FALSE(filter_config->textContentStreamingEnabled("non_existent_tool", "a.com", "/mcp"));

  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute route_proto;
  TestUtility::loadFromYaml(R"yaml(
tool_config:
  - tools:
      - name: tool_no_stream
        http_rule:
          get: "/no_stream"
    default_server_info:
      host: "a.com"
      path: "/mcp"
)yaml",
                            route_proto);

  auto per_route_config = std::make_unique<McpJsonRestBridgePerRouteConfig>(route_proto);

  EXPECT_FALSE(per_route_config->textContentStreamingEnabled("tool_no_stream", "a.com", "/mcp"));
  EXPECT_FALSE(per_route_config->textContentStreamingEnabled("non_existent_tool", "a.com", "/mcp"));
}

TEST_F(EndpointKeyedFilteringTest, ToolListLocalToolsHandlesDuplicateNames) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge filter_proto;
  TestUtility::loadFromYaml(R"yaml(
tool_config:
  tools:
    - name: duplicate_tool
      http_rule:
        get: "/exact"
  default_server_info:
    host: ""
    path: ""
  tool_list_local: {}
)yaml",
                            filter_proto);

  auto filter_config = std::make_shared<McpJsonRestBridgeFilterConfig>(filter_proto);

  auto tools = filter_config->toolListLocalTools("", "/mcp");
  ASSERT_EQ(tools.size(), 1);
  EXPECT_EQ(tools[0]->name(), "duplicate_tool");
}

TEST_F(EndpointKeyedFilteringTest, PerRouteToolListLocalReturnsFalse) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute route_proto;
  TestUtility::loadFromYaml(R"yaml(
tool_config:
  - tools:
      - name: tool_name
        http_rule:
          get: "/some_rule"
    default_server_info:
      host: "a.com"
      path: "/mcp"
)yaml",
                            route_proto);

  auto per_route_config = std::make_unique<McpJsonRestBridgePerRouteConfig>(route_proto);

  EXPECT_FALSE(per_route_config->toolListLocal("a.com", "/mcp"));
  EXPECT_FALSE(per_route_config->toolListLocal("b.com", "/other"));
}

TEST_F(EndpointKeyedFilteringTest, CustomAndDefaultEndpointPathMatching) {
  setupRouteConfig(R"yaml(
tool_config:
  - tools:
      - name: custom_tool
        http_rule:
          get: "/custom_response"
    default_server_info:
      host: "a.com"
      path: "/custom_path"
  - tools:
      - name: default_tool
        http_rule:
          get: "/default_response"
)yaml");

  // Case 1: Match the specific path "/custom_path" with host "a.com" (verifying port stripping
  // works)
  {
    request_headers_ = {
        {":method", "POST"}, {":path", "/custom_path"}, {":authority", "a.com:8080"}};
    recreateFilter();
    EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
              Http::FilterHeadersStatus::StopIteration);

    EXPECT_CALL(decoder_callbacks_, requestHeaders())
        .WillRepeatedly(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    Buffer::OwnedImpl body(
        R"json({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"custom_tool","arguments":{}}})json");
    filter_->decodeData(body, true);

    EXPECT_EQ(request_headers_.getPathValue(), "/custom_response");
  }

  // Case 2: Match the default path "/mcp" via the global default config (which was registered as
  // {"", "/mcp"})
  {
    request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {":authority", "b.com"}};
    recreateFilter();
    EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
              Http::FilterHeadersStatus::StopIteration);

    EXPECT_CALL(decoder_callbacks_, requestHeaders())
        .WillRepeatedly(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    Buffer::OwnedImpl body(
        R"json({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"default_tool","arguments":{}}})json");
    filter_->decodeData(body, true);

    EXPECT_EQ(request_headers_.getPathValue(), "/default_response");
  }

  // Case 3: A request with path "/unconfigured_path" should be bypassed / passed through
  {
    request_headers_ = {
        {":method", "POST"}, {":path", "/unconfigured_path"}, {":authority", "a.com"}};
    recreateFilter();
    EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);
  }
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
