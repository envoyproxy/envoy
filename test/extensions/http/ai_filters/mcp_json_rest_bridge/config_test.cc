#include <limits>

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"
#include "source/extensions/http/ai_filters/mcp_json_rest_bridge/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace McpJsonRestBridge {
namespace {

using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::LLMProtocol;
using BridgeProto = envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge;
using BridgePerRouteProto =
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute;

constexpr char DuplicateTools[] = R"yaml(
tools:
- name: get_api_key
  http_rule:
    get: "/v1/apiKeys"
- name: get_api_key
  http_rule:
    get: "/v2/apiKeys"
)yaml";

class McpJsonRestBridgeConfigTest : public testing::Test {
public:
  McpJsonRestBridgeFilterConfigFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
};

TEST_F(McpJsonRestBridgeConfigTest, IsRegistered) {
  auto* factory = Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory(
      "envoy.http.ai_filters.mcp_json_rest_bridge");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_THAT(factory, testing::WhenDynamicCastTo<McpJsonRestBridgeFilterConfigFactory*>(
                           testing::NotNull()));
}

// The AI filter is configured with the HTTP filter's messages.
TEST_F(McpJsonRestBridgeConfigTest, TakesTheHttpFilterConfiguration) {
  EXPECT_THAT(factory_.configTypes(),
              testing::UnorderedElementsAre(
                  "envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge",
                  "envoy.extensions.filters.http.mcp_json_rest_bridge.v3."
                  "McpJsonRestBridgePerRoute"));
}

TEST_F(McpJsonRestBridgeConfigTest, CreatesFilterFromEmptyConfig) {
  const auto factory_cb = factory_.createAiFilterFactory(*factory_.createEmptyConfigProto(),
                                                         context_, *stats_store_.rootScope());
  ASSERT_TRUE(factory_cb.ok()) << factory_cb.status();

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::Unspecified};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

TEST_F(McpJsonRestBridgeConfigTest, RejectsInvalidConfig) {
  BridgeProto proto;
  TestUtility::loadFromYaml(DuplicateTools, *proto.mutable_tool_config());
  const auto factory_cb =
      factory_.createAiFilterFactory(proto, context_, *stats_store_.rootScope());
  EXPECT_EQ(factory_cb.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(factory_cb.status().message(), testing::HasSubstr("Duplicate tool name"));
}

TEST_F(McpJsonRestBridgeConfigTest, CreatesRouteConfig) {
  BridgePerRouteProto proto;
  auto* tool = proto.add_tool_config()->add_tools();
  tool->set_name("get_api_key");
  tool->mutable_http_rule()->set_get("/v1/apiKeys");

  const auto route_config =
      factory_.createRouteConfig(proto, context_, context_.messageValidationVisitor());
  ASSERT_TRUE(route_config.ok()) << route_config.status();
  const auto* bridge_route_config =
      dynamic_cast<const HttpFilters::McpJsonRestBridge::McpJsonRestBridgePerRouteConfig*>(
          route_config->get());
  ASSERT_NE(bridge_route_config, nullptr);
  EXPECT_TRUE(bridge_route_config->hasEndpoint("host", "/mcp"));
}

TEST_F(McpJsonRestBridgeConfigTest, RejectsInvalidRouteConfig) {
  BridgePerRouteProto proto;
  TestUtility::loadFromYaml(DuplicateTools, *proto.add_tool_config());
  const auto route_config =
      factory_.createRouteConfig(proto, context_, context_.messageValidationVisitor());
  EXPECT_EQ(route_config.status().code(), absl::StatusCode::kInvalidArgument);
}

// Any string of a request within the body limit is one the bridge may have to read.
TEST_F(McpJsonRestBridgeConfigTest, AsksForStringsUpToTheBodyLimitInline) {
  BridgeProto proto;
  EXPECT_EQ(factory_.requestInlineStringBytes(proto), 64 * 1024);

  proto.mutable_max_request_body_size()->set_value(4096);
  EXPECT_EQ(factory_.requestInlineStringBytes(proto), 4096);

  proto.mutable_max_request_body_size()->set_value(0);
  EXPECT_EQ(factory_.requestInlineStringBytes(proto), std::numeric_limits<uint32_t>::max());
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
