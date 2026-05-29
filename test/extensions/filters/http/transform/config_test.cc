#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/transform/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {
namespace {

TEST(FactoryTest, FactoryTest) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context;
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.transform");
  ASSERT_NE(factory, nullptr);

  {
    const std::string config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: REPLACE
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
clear_route_cache: true
  )EOF";

    ProtoConfig per_route_proto_config;
    TestUtility::loadFromYaml(config, per_route_proto_config);
    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context).value();
    Http::MockFilterChainFactoryCallbacks filter_callbacks;
    EXPECT_CALL(filter_callbacks, addStreamFilter(_));
    cb(filter_callbacks);

    EXPECT_NE(nullptr, factory
                           ->createRouteSpecificFilterConfig(
                               per_route_proto_config, mock_factory_context.server_factory_context_,
                               mock_factory_context.messageValidationVisitor())
                           .value());
  }

  {
    const std::string config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
clear_route_cache: true
clear_cluster_cache: true
  )EOF";

    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb_or_error =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context);
    EXPECT_FALSE(cb_or_error.status().ok());
    EXPECT_EQ("Only one of clear_cluster_cache and clear_route_cache can be set to true",
              cb_or_error.status().message());
  }
}

} // namespace
} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
