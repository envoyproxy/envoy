#include <string>

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.validate.h"

#include "source/extensions/filters/http/lua/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
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
  EXPECT_NO_THROW(LuaFilterConfig()
                      .createFilterFactoryFromProto(
                          envoy::extensions::filters::http::lua::v3::Lua(), "stats", context)
                      .status()
                      .IgnoreError());
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
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(LuaFilterConfigTest, LuaFilterWithDefaultSourceCodeWithServerContext) {
  const std::string yaml_string = R"EOF(
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:headers():add("code", "code_from_hello")
      end
  )EOF";

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  LuaFilterConfig factory;
  Server::Configuration::ExtraFactoryContext extra_context{context.messageValidationVisitor(),
                                                           "stats"};
  Http::FilterFactoryCb cb =
      factory.createHttpFilterFactoryFromProto(proto_config, context, extra_context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

// An empty search pattern would contribute nothing but a stray ';' to package.path, so the
// package path fields reject one rather than silently accepting it.
TEST(LuaFilterConfigTest, EmptyPackagePathPatternIsRejected) {
  {
    envoy::extensions::filters::http::lua::v3::Lua proto_config;
    proto_config.add_package_paths("");
    EXPECT_THROW_WITH_REGEX(TestUtility::validate(proto_config), ProtoValidationException,
                            "PackagePaths.*value length must be at least 1");
  }
  {
    envoy::extensions::filters::http::lua::v3::Lua proto_config;
    proto_config.add_package_cpaths("");
    EXPECT_THROW_WITH_REGEX(TestUtility::validate(proto_config), ProtoValidationException,
                            "PackageCpaths.*value length must be at least 1");
  }
  {
    envoy::extensions::filters::http::lua::v3::LuaPerRoute proto_config;
    proto_config.add_package_paths("");
    EXPECT_THROW_WITH_REGEX(TestUtility::validate(proto_config), ProtoValidationException,
                            "PackagePaths.*value length must be at least 1");
  }
  {
    envoy::extensions::filters::http::lua::v3::LuaPerRoute proto_config;
    proto_config.add_package_cpaths("");
    EXPECT_THROW_WITH_REGEX(TestUtility::validate(proto_config), ProtoValidationException,
                            "PackageCpaths.*value length must be at least 1");
  }
}

// The rule is per entry, so several non-empty patterns are accepted on both messages. Without this
// the test above would also pass against a rule which rejected everything.
TEST(LuaFilterConfigTest, PackagePathPatternsAreAccepted) {
  {
    envoy::extensions::filters::http::lua::v3::Lua proto_config;
    proto_config.add_package_paths("/etc/envoy/lua/?.lua");
    proto_config.add_package_paths("/etc/envoy/lua/?/init.lua");
    proto_config.add_package_cpaths("/etc/envoy/lua/?.so");
    EXPECT_NO_THROW(TestUtility::validate(proto_config));
  }
  {
    envoy::extensions::filters::http::lua::v3::LuaPerRoute proto_config;
    proto_config.add_package_paths("/etc/envoy/lua/?.lua");
    proto_config.add_package_cpaths("/etc/envoy/lua/?.so");
    EXPECT_NO_THROW(TestUtility::validate(proto_config));
  }
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
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
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
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
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
  EXPECT_THAT(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).status(),
      StatusHelpers::HasStatusMessage(
          "Error: Only one of `inline_code` or `default_source_code` can be set for the Lua "
          "filter."));
}
#endif

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
