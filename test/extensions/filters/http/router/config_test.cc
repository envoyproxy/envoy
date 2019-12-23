#include <string>

#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/config/filter/http/router/v2/router.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/router/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {
namespace {

TEST(RouterFilterConfigTest, SimpleRouterFilterConfig) {
  const std::string yaml_string = R"EOF(
  dynamic_stats: true
  start_child_span: true
  )EOF";

  envoy::config::filter::http::router::v2::Router proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, BadRouterFilterConfig) {
  const std::string yaml_string = R"EOF(
  dynamic_stats: true
  route: {}
  )EOF";

  envoy::config::filter::http::router::v2::Router proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYaml(yaml_string, proto_config), EnvoyException,
                          "route: Cannot find field");
}

TEST(RouterFilterConfigTest, RouterFilterWithUnsupportedStrictHeaderCheck) {
  const std::string yaml = R"EOF(
  strict_check_headers:
  - unsupportedHeader
  )EOF";

  envoy::config::filter::http::router::v2::Router router_config;
  TestUtility::loadFromYaml(yaml, router_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(router_config, "stats.", context),
      ProtoValidationException,
      "Proto constraint validation failed (RouterValidationError.StrictCheckHeaders[i]: "
      "[\"value must be in list \" ["
      "\"x-envoy-upstream-rq-timeout-ms\" "
      "\"x-envoy-upstream-rq-per-try-timeout-ms\" "
      "\"x-envoy-max-retries\" "
      "\"x-envoy-retry-grpc-on\" "
      "\"x-envoy-retry-on\""
      "]]): strict_check_headers: \"unsupportedHeader\"\n");
}

TEST(RouterFilterConfigTest, RouterV2Filter) {
  envoy::config::filter::http::router::v2::Router router_config;
  router_config.mutable_dynamic_stats()->set_value(true);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(router_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, RouterFilterWithEmptyProtoConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<RouterFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>()),
      EnvoyException,
      fmt::format("Double registration for name: '{}'", HttpFilterNames::get().Router));
}

} // namespace
} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
