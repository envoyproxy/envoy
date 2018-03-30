#include "common/config/filter_json.h"

#include "extensions/filters/http/ratelimit/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

TEST(HttpFilterConfigTest, RateLimitFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitFilterConfig factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, RateLimitFilterCorrectProto) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config{};
  Envoy::Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitFilterConfig factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, RateLimitFilterEmptyProto) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitFilterConfig factory;

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config =
      *dynamic_cast<envoy::config::filter::http::rate_limit::v2::RateLimit*>(
          factory.createEmptyConfigProto().get());
  Envoy::Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config);

  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, BadRateLimitFilterConfig) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 0
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
