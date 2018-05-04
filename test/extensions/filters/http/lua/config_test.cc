#include "extensions/filters/http/lua/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

TEST(LuaFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(LuaFilterConfig().createFilterFactoryFromProto(
                   envoy::config::filter::http::lua::v2::Lua(), "stats", context),
               ProtoValidationException);
}

TEST(LuaFilterConfigTest, LuaFilterInJson) {
  std::string json_string = R"EOF(
  {
    "inline_code" : "print(5)"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  LuaFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
