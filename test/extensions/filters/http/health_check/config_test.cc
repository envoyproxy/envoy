#include "extensions/filters/http/health_check/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

TEST(HealthCheckFilterConfig, HealthCheckFilter) {
  std::string json_string = R"EOF(
  {
    "pass_through_mode" : true,
    "endpoint" : "/hc"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  HealthCheckFilterConfig factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HealthCheckFilterConfig, BadHealthCheckFilterConfig) {
  std::string json_string = R"EOF(
  {
    "pass_through_mode" : true,
    "endpoint" : "/hc",
    "status" : 500
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  HealthCheckFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(HealthCheckFilterConfig, FailsWhenNotPassThroughButTimeoutSetJson) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  Json::ObjectSharedPtr config = Json::Factory::loadFromString(
      "{\"pass_through_mode\":false, \"cache_time_ms\":234, \"endpoint\":\"foo\"}");
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(healthCheckFilterConfig.createFilterFactory(*config, "dummy_stats_prefix", context),
               EnvoyException);
}

TEST(HealthCheckFilterConfig, NotFailingWhenNotPassThroughAndTimeoutNotSetJson) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  Json::ObjectSharedPtr config =
      Json::Factory::loadFromString("{\"pass_through_mode\":false, \"endpoint\":\"foo\"}");
  NiceMock<Server::Configuration::MockFactoryContext> context;

  healthCheckFilterConfig.createFilterFactory(*config, "dummy_stats_prefix", context);
}

TEST(HealthCheckFilterConfig, FailsWhenNotPassThroughButTimeoutSetProto) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  envoy::config::filter::http::health_check::v2::HealthCheck config{};
  NiceMock<Server::Configuration::MockFactoryContext> context;

  config.mutable_pass_through_mode()->set_value(false);
  config.set_endpoint("foo");
  config.mutable_cache_time()->set_seconds(10);

  EXPECT_THROW(
      healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context),
      EnvoyException);
}

TEST(HealthCheckFilterConfig, NotFailingWhenNotPassThroughAndTimeoutNotSetProto) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  envoy::config::filter::http::health_check::v2::HealthCheck config{};
  NiceMock<Server::Configuration::MockFactoryContext> context;

  config.mutable_pass_through_mode()->set_value(false);
  config.set_endpoint("foo");
  healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context);
}

TEST(HealthCheckFilterConfig, HealthCheckFilterWithEmptyProto) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::config::filter::http::health_check::v2::HealthCheck config =
      *dynamic_cast<envoy::config::filter::http::health_check::v2::HealthCheck*>(
          healthCheckFilterConfig.createEmptyConfigProto().get());

  config.mutable_pass_through_mode()->set_value(false);
  config.set_endpoint("foo");
  healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context);
}

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
