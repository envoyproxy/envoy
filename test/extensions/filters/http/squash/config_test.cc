#include "envoy/config/filter/http/squash/v2/squash.pb.validate.h"

#include "extensions/filters/http/squash/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {

TEST(SquashFilterConfigFactoryTest, SquashFilterCorrectJson) {
  std::string json_string = R"EOF(
    {
      "cluster" : "fake_cluster",
      "attachment_template" : {"a":"b"},
      "request_timeout_ms" : 1001,
      "attachment_poll_period_ms" : 2002,
      "attachment_timeout_ms" : 3003
    }
    )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SquashFilterConfigFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
