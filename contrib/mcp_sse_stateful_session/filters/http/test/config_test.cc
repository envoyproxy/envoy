#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "contrib/mcp_sse_stateful_session/filters/http/source/config.h"
#include "contrib/mcp_sse_stateful_session/filters/http/test/mocks/mcp_sse_stateful_session.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpSseStatefulSession {
namespace {

constexpr absl::string_view ConfigYaml = R"EOF(
sse_session_state:
  name: "envoy.http.sse_stateful_session.mock"
  typed_config: {}
)EOF";

constexpr absl::string_view DisableYaml = R"EOF(
disabled: true
)EOF";

constexpr absl::string_view RouteConfigYaml = R"EOF(
mcp_sse_stateful_session:
  sse_session_state:
    name: "envoy.http.sse_stateful_session.mock"
    typed_config: {}
)EOF";

constexpr absl::string_view NotExistYaml = R"EOF(
mcp_sse_stateful_session:
  sse_session_state:
    name: "envoy.http.mcp_sse_stateful_session.not_exist"
    typed_config: {}
)EOF";

constexpr absl::string_view EmptyStatefulSessionRouteYaml = R"EOF(
mcp_sse_stateful_session: {}
)EOF";

TEST(StatefulSessionFactoryConfigTest, SimpleConfigTest) {
  testing::NiceMock<Envoy::Http::MockSseSessionStateFactoryConfig> config_factory;
  Registry::InjectFactory<Envoy::Http::SseSessionStateFactoryConfig> registration(
      config_factory);

  ProtoConfig proto_config;
  PerRouteProtoConfig proto_route_config;
  PerRouteProtoConfig disabled_config;
  PerRouteProtoConfig not_exist_config;
  ProtoConfig empty_proto_config;
  PerRouteProtoConfig empty_proto_route_config;

  TestUtility::loadFromYamlAndValidate(std::string(ConfigYaml), proto_config);
  TestUtility::loadFromYamlAndValidate(std::string(RouteConfigYaml), proto_route_config);
  TestUtility::loadFromYamlAndValidate(std::string(DisableYaml), disabled_config);
  TestUtility::loadFromYamlAndValidate(std::string(NotExistYaml), not_exist_config);
  TestUtility::loadFromYamlAndValidate(std::string(EmptyStatefulSessionRouteYaml),
                                       empty_proto_route_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  McpSseStatefulSessionFactoryConfig factory;

  Envoy::Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Envoy::Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);

  EXPECT_TRUE(factory
                  .createRouteSpecificFilterConfig(proto_route_config, server_context,
                                                   context.messageValidationVisitor())
                  .ok());
  EXPECT_TRUE(factory
                  .createRouteSpecificFilterConfig(disabled_config, server_context,
                                                   context.messageValidationVisitor())
                  .ok());
  EXPECT_THROW_WITH_MESSAGE(factory
                                .createRouteSpecificFilterConfig(not_exist_config, server_context,
                                                                 context.messageValidationVisitor())
                                .value(),
                            EnvoyException,
                            "Didn't find a registered implementation for name: "
                            "'envoy.http.mcp_sse_stateful_session.not_exist'");

  EXPECT_NO_THROW(factory.createFilterFactoryFromProto(empty_proto_config, "stats", context)
                      .status()
                      .IgnoreError());
  EXPECT_TRUE(factory
                  .createRouteSpecificFilterConfig(empty_proto_route_config, server_context,
                                                   context.messageValidationVisitor())
                  .ok());
}

} // namespace
} // namespace McpSseStatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
