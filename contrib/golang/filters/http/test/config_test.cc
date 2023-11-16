#include <string>

#include "envoy/registry/registry.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "contrib/golang/filters/http/source/config.h"
#include "contrib/golang/filters/http/source/golang_filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {
namespace {

std::string genSoPath(std::string name) {
  return TestEnvironment::substitute(
      "{{ test_rundir }}/contrib/golang/filters/http/test/test_data/" + name + "/filter.so");
}

TEST(GolangFilterConfigTest, InvalidateEmptyConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      GolangFilterConfig()
          .createFilterFactoryFromProto(envoy::extensions::filters::http::golang::v3alpha::Config(),
                                        "stats", context)
          .status()
          .IgnoreError(),
      Envoy::ProtoValidationException,
      "ConfigValidationError.LibraryId: value length must be at least 1 characters");
}

TEST(GolangFilterConfigTest, GolangFilterWithValidConfig) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: xxx
  merge_policy: MERGE_VIRTUALHOST_ROUTER_FILTER
  plugin_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
    type_url: typexx
    value:
        key: value
        int: 10
  )EOF";

  const std::string PASSTHROUGH{"passthrough"};
  auto yaml_string = absl::StrFormat(yaml_fmt, PASSTHROUGH, genSoPath(PASSTHROUGH));
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GolangFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  EXPECT_CALL(filter_callback, addAccessLogHandler(_));
  auto plugin_config = proto_config.plugin_config();
  std::string str;
  EXPECT_TRUE(plugin_config.SerializeToString(&str));
  cb(filter_callback);
}

TEST(GolangFilterConfigTest, GolangFilterWithNilPluginConfig) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: xxx
  )EOF";

  const std::string PASSTHROUGH{"passthrough"};
  auto yaml_string = absl::StrFormat(yaml_fmt, PASSTHROUGH, genSoPath(PASSTHROUGH));
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GolangFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  EXPECT_CALL(filter_callback, addAccessLogHandler(_));
  auto plugin_config = proto_config.plugin_config();
  std::string str;
  EXPECT_TRUE(plugin_config.SerializeToString(&str));
  cb(filter_callback);
}

} // namespace
} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
