#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using ::testing::HasSubstr;
using ::testing::NiceMock;

TEST(McpJsonRestBridgeFilterConfigFactoryTest, RegisterAndCreateFilterWithEmptyConfig) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.mcp_json_rest_bridge");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::StatusOr<Http::FilterFactoryCb> cb =
      factory->createFilterFactoryFromProto(proto_config, "stats", context);
  ASSERT_OK(cb);

  // TODO(paulhong01): Update the following verification once the proto config is processed
  // properly.
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter);
  (*cb)(filter_callbacks);
}

TEST(McpJsonRestBridgeFilterConfigFactoryTest, CreateFilterWithServerContext) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  McpJsonRestBridgeFilterConfigFactory factory;
  Http::FilterFactoryCb cb =
      factory.createHttpFilterFactoryFromProto(proto_config, "stats", server_context).value();

  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter);
  cb(filter_callbacks);
}

TEST(McpJsonRestBridgeFilterConfigTest, InvalidToolListHttpRuleThrowsException) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  TestUtility::loadFromYaml(R"EOF(
    tool_config:
      tool_list_http_rule:
        post: "/discovery/v1/service/foo.googleapis.com/mcptools"
  )EOF",
                            proto_config);

  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, "stats", context),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("tool_list_http_rule must be a GET request with an empty body")));

  TestUtility::loadFromYaml(R"EOF(
    tool_config:
      tool_list_http_rule:
        get: "/discovery/v1/service/foo.googleapis.com/mcptools"
        body: "*"
  )EOF",
                            proto_config);

  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, "stats", context),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("tool_list_http_rule must be a GET request with an empty body")));
}

TEST(McpJsonRestBridgeFilterConfigTest, IncRequestCountWithToolParam) {
  Stats::IsolatedStoreImpl stats_store;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  McpJsonRestBridgeFilterConfig config(proto_config, *stats_store.rootScope());

  config.incRequestCount("tools/call", "create_api_key", "mcp_json_rest_bridge_ok");

  Stats::CounterSharedPtr counter = TestUtility::findCounter(
      stats_store, "http.mcp_json_rest_bridge.request_count.mcp_method.tools/"
                   "call.mcp_param.create_api_key.status.mcp_json_rest_bridge_ok");
  ASSERT_NE(counter, nullptr);
  EXPECT_EQ(counter->value(), 1);
  EXPECT_THAT(counter->tags(),
              testing::ElementsAre(Stats::Tag{"mcp_method", "tools/call"},
                                   Stats::Tag{"mcp_param", "create_api_key"},
                                   Stats::Tag{"status", "mcp_json_rest_bridge_ok"}));
}

TEST(McpJsonRestBridgeFilterConfigTest, IncRequestCountWithoutToolParam) {
  Stats::IsolatedStoreImpl stats_store;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  McpJsonRestBridgeFilterConfig config(proto_config, *stats_store.rootScope());

  config.incRequestCount("initialize", "", "mcp_json_rest_bridge_ok");

  Stats::CounterSharedPtr counter = TestUtility::findCounter(
      stats_store,
      "http.mcp_json_rest_bridge.request_count.mcp_method.initialize.status.mcp_json_rest_bridge_"
      "ok");
  ASSERT_NE(counter, nullptr);
  EXPECT_EQ(counter->value(), 1);
  EXPECT_THAT(counter->tags(),
              testing::ElementsAre(Stats::Tag{"mcp_method", "initialize"},
                                   Stats::Tag{"status", "mcp_json_rest_bridge_ok"}));
}

TEST(McpJsonRestBridgeFilterConfigTest, IncRequestCountAccumulates) {
  Stats::IsolatedStoreImpl stats_store;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  McpJsonRestBridgeFilterConfig config(proto_config, *stats_store.rootScope());

  config.incRequestCount("tools/call", "list_api_keys", "mcp_json_rest_bridge_ok");
  config.incRequestCount("tools/call", "list_api_keys", "mcp_json_rest_bridge_ok");

  Stats::CounterSharedPtr counter = TestUtility::findCounter(
      stats_store, "http.mcp_json_rest_bridge.request_count.mcp_method.tools/"
                   "call.mcp_param.list_api_keys.status.mcp_json_rest_bridge_ok");
  ASSERT_NE(counter, nullptr);
  EXPECT_EQ(counter->value(), 2);
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
