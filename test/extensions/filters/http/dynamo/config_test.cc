#include "extensions/filters/http/dynamo/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

TEST(DynamoFilterConfigTest, DynamoFilter) {
  std::string json_string = R"EOF(
  {
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  DynamoFilterConfig factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
