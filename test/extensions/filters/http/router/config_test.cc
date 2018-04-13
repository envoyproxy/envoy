#include "envoy/registry/registry.h"

#include "extensions/filters/http/router/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

TEST(RouterFilterConfigTest, RouterFilterInJson) {
  std::string json_string = R"EOF(
  {
    "dynamic_stats" : true,
    "start_child_span" : true
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, BadRouterFilterConfig) {
  std::string json_string = R"EOF(
  {
    "dynamic_stats" : true,
    "route" : {}
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(RouterFilterConfigTest, RouterV2Filter) {
  envoy::config::filter::http::router::v2::Router router_config;
  router_config.mutable_dynamic_stats()->set_value(true);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(router_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, RouterFilterWithEmptyProtoConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<RouterFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>()),
      EnvoyException,
      fmt::format("Double registration for name: '{}'", HttpFilterNames::get().ROUTER));
}

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
