#include "source/extensions/filters/http/stateful_session/config.h"

#include "test/mocks/http/stateful_session.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {
namespace {

constexpr absl::string_view ConfigYaml = R"EOF(
session_state:
  name: "envoy.http.stateful_session.mock"
  typed_config: {}
)EOF";

constexpr absl::string_view DisableYaml = R"EOF(
disabled: true
)EOF";

constexpr absl::string_view RouteConfigYaml = R"EOF(
stateful_session:
  session_state:
    name: "envoy.http.stateful_session.mock"
    typed_config: {}
)EOF";

constexpr absl::string_view NotExistYaml = R"EOF(
stateful_session:
  session_state:
    name: "envoy.http.stateful_session.not_exist"
    typed_config: {}
)EOF";

constexpr absl::string_view EmptyStatefulSessionRouteYaml = R"EOF(
stateful_session: {}
)EOF";

TEST(StatefulSessionFactoryConfigTest, SimpleConfigTest) {
  testing::NiceMock<Http::MockSessionStateFactoryConfig> config_factory;
  Registry::InjectFactory<Http::SessionStateFactoryConfig> registration(config_factory);

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
  StatefulSessionFactoryConfig factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);

  EXPECT_NO_THROW(factory.createRouteSpecificFilterConfig(proto_route_config, server_context,
                                                          context.messageValidationVisitor()));
  EXPECT_NO_THROW(factory.createRouteSpecificFilterConfig(disabled_config, server_context,
                                                          context.messageValidationVisitor()));
  EXPECT_THROW_WITH_MESSAGE(
      factory.createRouteSpecificFilterConfig(not_exist_config, server_context,
                                              context.messageValidationVisitor()),
      EnvoyException,
      "Didn't find a registered implementation for name: 'envoy.http.stateful_session.not_exist'");

  EXPECT_NO_THROW(factory.createFilterFactoryFromProto(empty_proto_config, "stats", context)
                      .status()
                      .IgnoreError());
  EXPECT_NO_THROW(factory.createRouteSpecificFilterConfig(empty_proto_route_config, server_context,
                                                          context.messageValidationVisitor()));
}

} // namespace
} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
