#include <string>

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.validate.h"

#include "source/extensions/filters/http/lua/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {
namespace {

TEST(LuaFilterConfigTest, ValidateEmptyConfigNotFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NO_THROW(LuaFilterConfig().createFilterFactoryFromProto(
      envoy::extensions::filters::http::lua::v3::Lua(), "stats", context));
}

TEST(LuaFilterConfigTest, LuaFilterWithDefaultSourceCode) {
  const std::string yaml_string = R"EOF(
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:headers():add("code", "code_from_hello")
      end
  )EOF";

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  LuaFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
TEST(LuaFilterConfigTest, LuaFilterInJson) {
  const std::string yaml_string = R"EOF(
  inline_code : "print(5)"
  )EOF";

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  LuaFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(LuaFilterConfigTest, LuaFilterWithDeprecatedInlineCode) {
  const std::string yaml_string = R"EOF(
  inline_code: |
    function envoy_on_request(request_handle)
      request_handle:headers():add("code", "code_from_hello")
    end
  )EOF";

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  LuaFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(LuaFilterConfigTest, LuaFilterWithBothDeprecatedInlineCodeAndDefaultSourceCode) {
  const std::string yaml_string = R"EOF(
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:headers():add("code", "code_from_hello")
      end
  inline_code: |
    function envoy_on_request(request_handle)
      request_handle:headers():add("code", "code_from_hello")
    end
  )EOF";

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  LuaFilterConfig factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(proto_config, "stats", context), EnvoyException,
      "Error: Only one of `inline_code` or `default_source_code` can be set for the Lua filter.");
}
#endif

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
