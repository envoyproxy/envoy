#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/header_mutation/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {
namespace {

TEST(FactoryTest, FactoryTest) {

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;

  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.header_mutation");
  ASSERT_NE(factory, nullptr);

  const std::string config = R"EOF(
  mutations:
    request_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%REQ(ANOTHER-FLAG-HEADER)%"
        append_action: APPEND_IF_EXISTS_OR_ADD
    response_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%REQ(ANOTHER-FLAG-HEADER)%"
        append_action: APPEND_IF_EXISTS_OR_ADD
  )EOF";

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(config, per_route_proto_config);
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context;

  auto cb =
      factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);

  EXPECT_NE(nullptr, factory->createRouteSpecificFilterConfig(
                         per_route_proto_config, mock_factory_context.server_factory_context_,
                         mock_factory_context.messageValidationVisitor()));
}

TEST(FactoryTest, UpstreamFactoryTest) {

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;

  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::UpstreamHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.header_mutation");
  ASSERT_NE(factory, nullptr);
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
