#include <string>

#include "envoy/extensions/filters/http/lua/v3alpha/lua.pb.h"
#include "envoy/extensions/filters/http/lua/v3alpha/lua.pb.validate.h"

#include "extensions/filters/http/lua/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {
namespace {

TEST(LuaFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(LuaFilterConfig().createFilterFactoryFromProto(
                   envoy::extensions::filters::http::lua::v3alpha::Lua(), "stats", context),
               ProtoValidationException);
}

TEST(LuaFilterConfigTest, LuaFilterInJson) {
  const std::string yaml_string = R"EOF(
  inline_code : "print(5)"
  )EOF";

  envoy::extensions::filters::http::lua::v3alpha::Lua proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  LuaFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
