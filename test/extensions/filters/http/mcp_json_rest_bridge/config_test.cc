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
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};
  Http::FilterFactoryCb cb =
      factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context).value();

  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter);
  cb(filter_callbacks);
}

TEST(McpJsonRestBridgeFilterConfigTest, InvalidToolListHttpRule) {
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

TEST(McpJsonRestBridgeFilterConfigTest, DuplicateToolNames) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  TestUtility::loadFromYaml(R"EOF(
    tool_config:
      tools:
        - name: "my_tool"
          http_rule: { get: "/foo" }
        - name: "my_tool"
          http_rule: { get: "/bar" }
  )EOF",
                            proto_config);

  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THAT(
      factory.createFilterFactoryFromProto(proto_config, "stats", context),
      HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("Duplicate tool name: my_tool")));
}

TEST(McpJsonRestBridgeFilterPerRouteConfigTest, PerRouteConfigDuplicateToolNames) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute
      per_route_config;
  TestUtility::loadFromYaml(R"EOF(
    tool_config:
      tools:
        - name: "my_tool"
          http_rule: { get: "/foo" }
        - name: "my_tool"
          http_rule: { get: "/bar" }
  )EOF",
                            per_route_config);

  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto config_or = factory.createRouteSpecificFilterConfig(
      per_route_config, context, ProtobufMessage::getNullValidationVisitor());
  EXPECT_THAT(config_or, HasStatus(absl::StatusCode::kInvalidArgument,
                                   HasSubstr("Duplicate tool name: my_tool")));
}

TEST(McpJsonRestBridgeFilterConfigTest, InvalidMaxSupportedProtocolVersion) {
  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  const std::vector<std::string> invalid_versions = {
      "invalid-version", "2024-11-05", "2025-03-26", "2025-06-18", "2026-99-99", "", "2026-11-25"};

  for (const auto& version : invalid_versions) {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    TestUtility::loadFromYaml(fmt::format(R"EOF(
      server_info:
        max_supported_protocol_version: "{}"
    )EOF",
                                          version),
                              proto_config);

    // Proto-level validation (PGV) fails when loaded via factory.
    EXPECT_THROW_WITH_REGEX(
        factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
        Envoy::ProtoValidationException, "Proto constraint validation failed");
  }
}

TEST(McpJsonRestBridgeFilterConfigTest, MaxSupportedProtocolVersionBehavior) {
  // Default when not provided: 2025-11-25
  {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    absl::StatusOr<McpJsonRestBridgeFilterConfigSharedPtr> config =
        McpJsonRestBridgeFilterConfig::create(proto_config);
    ASSERT_OK(config);
    EXPECT_EQ((*config)->maxSupportedProtocolVersion(), "2025-11-25");
    EXPECT_TRUE((*config)->supportsProtocolVersion("2025-11-25"));
    EXPECT_FALSE((*config)->supportsProtocolVersion("2026-07-28"));
  }

  // Version 2025-11-25 is effective
  {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    TestUtility::loadFromYaml(R"EOF(
      server_info:
        max_supported_protocol_version: "2025-11-25"
    )EOF",
                              proto_config);
    absl::StatusOr<McpJsonRestBridgeFilterConfigSharedPtr> config =
        McpJsonRestBridgeFilterConfig::create(proto_config);
    ASSERT_OK(config);
    EXPECT_EQ((*config)->maxSupportedProtocolVersion(), "2025-11-25");
    EXPECT_TRUE((*config)->supportsProtocolVersion("2025-11-25"));
    EXPECT_FALSE((*config)->supportsProtocolVersion("2026-07-28"));
  }

  // Version 2026-07-28 is effective
  {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    TestUtility::loadFromYaml(R"EOF(
      server_info:
        max_supported_protocol_version: "2026-07-28"
    )EOF",
                              proto_config);
    absl::StatusOr<McpJsonRestBridgeFilterConfigSharedPtr> config =
        McpJsonRestBridgeFilterConfig::create(proto_config);
    ASSERT_OK(config);
    EXPECT_EQ((*config)->maxSupportedProtocolVersion(), "2026-07-28");
    EXPECT_TRUE((*config)->supportsProtocolVersion("2025-11-25"));
    EXPECT_TRUE((*config)->supportsProtocolVersion("2026-07-28"));
  }
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
