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
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::SizeIs;

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
  }
}

TEST(McpJsonRestBridgeFilterConfigTest, ValidMcpParamHeaders) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  TestUtility::loadFromYaml(R"EOF(
    tool_config:
      tools:
        - name: "my_tool"
          http_rule: { get: "/foo" }
          mcp_param_headers:
            - { name: "Region", property_path: ["region"], type: STRING }
            - { name: "Count", property_path: ["user.count"], type: INTEGER }
            - { name: "Dry-Run", property_path: ["options", "dry_run"], type: BOOLEAN }
        - name: "other_tool"
          http_rule: { get: "/bar" }
          mcp_param_headers:
            - { name: "region", property_path: ["region"], type: STRING }
  )EOF",
                            proto_config);

  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_OK(factory.createFilterFactoryFromProto(proto_config, "stats", context));

  absl::StatusOr<McpJsonRestBridgeFilterConfigSharedPtr> config =
      McpJsonRestBridgeFilterConfig::create(proto_config);
  ASSERT_OK(config);
  const std::vector<McpParamHeaderMapping>& mappings =
      (*config)->mcpParamHeaders("my_tool", "", "/mcp");
  ASSERT_EQ(mappings.size(), 3);
  EXPECT_EQ(mappings[0].header_name.get(), "mcp-param-region");
  EXPECT_THAT(mappings[0].property_path, ElementsAre("region"));
  EXPECT_EQ(mappings[0].type,
            envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpParamHeader::STRING);
  EXPECT_EQ(mappings[1].header_name.get(), "mcp-param-count");
  EXPECT_THAT(mappings[1].property_path, ElementsAre("user.count"));
  EXPECT_EQ(mappings[1].type,
            envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpParamHeader::INTEGER);
  EXPECT_EQ(mappings[2].header_name.get(), "mcp-param-dry-run");
  EXPECT_THAT(mappings[2].property_path, ElementsAre("options", "dry_run"));
  EXPECT_EQ(mappings[2].type,
            envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpParamHeader::BOOLEAN);
  EXPECT_THAT((*config)->mcpParamHeaders("other_tool", "", "/mcp"), SizeIs(1));
}

TEST(McpJsonRestBridgeFilterConfigTest, DuplicateMcpParamHeaderNamesIgnoringCase) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  TestUtility::loadFromYaml(R"EOF(
    tool_config:
      tools:
        - name: "my_tool"
          http_rule: { get: "/foo" }
          mcp_param_headers:
            - { name: "Region", property_path: ["region"], type: STRING }
            - { name: "region", property_path: ["other_region"], type: STRING }
  )EOF",
                            proto_config);

  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, "stats", context),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Duplicate Mcp-Param header name: region (tool: my_tool)")));
}

TEST(McpJsonRestBridgeFilterPerRouteConfigTest, PerRouteConfigDuplicateMcpParamHeaderNames) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute
      per_route_config;
  TestUtility::loadFromYaml(R"EOF(
    tool_config:
      tools:
        - name: "my_tool"
          http_rule: { get: "/foo" }
          mcp_param_headers:
            - { name: "Region", property_path: ["region"], type: STRING }
            - { name: "REGION", property_path: ["other_region"], type: STRING }
  )EOF",
                            per_route_config);

  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto config_or = factory.createRouteSpecificFilterConfig(
      per_route_config, context, ProtobufMessage::getNullValidationVisitor());
  EXPECT_THAT(config_or,
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Duplicate Mcp-Param header name: REGION (tool: my_tool)")));
}

TEST(McpJsonRestBridgeFilterConfigTest, InvalidMcpParamHeaderFields) {
  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  const std::vector<std::string> invalid_mappings = {
      // Empty name.
      R"({ name: "", property_path: ["region"], type: STRING })",
      // Names that are not an HTTP header field name token.
      R"({ name: "Region Code", property_path: ["region"], type: STRING })",
      R"({ name: "R\u00e9gion", property_path: ["region"], type: STRING })",
      // Empty property path.
      R"({ name: "Region", property_path: [], type: STRING })",
      // Missing or unspecified type.
      R"({ name: "Region", property_path: ["region"] })",
      R"({ name: "Region", property_path: ["region"], type: TYPE_UNSPECIFIED })",
  };

  for (const auto& mapping : invalid_mappings) {
    SCOPED_TRACE(mapping);
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    TestUtility::loadFromYaml(fmt::format(R"EOF(
      tool_config:
        tools:
          - name: "my_tool"
            http_rule: {{ get: "/foo" }}
            mcp_param_headers:
              - {}
    )EOF",
                                          mapping),
                              proto_config);

    EXPECT_THROW_WITH_REGEX(
        factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
        Envoy::ProtoValidationException, "Proto constraint validation failed");
  }
}

TEST(McpJsonRestBridgeFilterConfigTest, UnsupportedMcpParamHeaderType) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  auto* tool = proto_config.mutable_tool_config()->add_tools();
  tool->set_name("my_tool");
  tool->mutable_http_rule()->set_get("/foo");
  auto* mapping = tool->add_mcp_param_headers();
  mapping->set_name("Region");
  mapping->add_property_path("region");
  mapping->set_type(
      static_cast<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpParamHeader::Type>(
          99));

  McpJsonRestBridgeFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
      Envoy::ProtoValidationException, "Proto constraint validation failed");
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
