#include "common/config/filter_json.h"

#include "extensions/filters/network/ratelimit/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

TEST(RateLimitFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RateLimitConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::rate_limit::v2::RateLimit(), context),
               ProtoValidationException);
}

TEST(RateLimitFilterConfigTest, RatelimitCorrectJson) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "domain" : "fake_domain",
    "descriptors": [[{ "key" : "my_key",  "value" : "my_value" }]],
    "timeout_ms": 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitConfigFactory factory;
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RateLimitFilterConfigTest, RatelimitCorrectProto) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "domain" : "fake_domain",
    "descriptors": [[{ "key" : "my_key",  "value" : "my_value" }]],
    "timeout_ms": 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config{};
  Envoy::Config::FilterJson::translateTcpRateLimitFilter(*json_config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitConfigFactory factory;
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RateLimitFilterConfigTest, RatelimitEmptyProto) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "domain" : "fake_domain",
    "descriptors": [[{ "key" : "my_key",  "value" : "my_value" }]],
    "timeout_ms": 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitConfigFactory factory;
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config =
      *dynamic_cast<envoy::config::filter::network::rate_limit::v2::RateLimit*>(
          factory.createEmptyConfigProto().get());
  Envoy::Config::FilterJson::translateTcpRateLimitFilter(*json_config, proto_config);

  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
