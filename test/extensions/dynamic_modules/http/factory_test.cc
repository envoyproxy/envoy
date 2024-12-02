#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

TEST(DynamicModuleConfigFactory, Overrides) {
  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.extensions.filters.http.dynamic_modules");
  auto empty_config = factory.createEmptyConfigProto();
  EXPECT_NE(empty_config, nullptr);
}

TEST(DynamicModuleConfigFactory, LoadOK) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
filter_name: foo
filter_config: bar
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
  EXPECT_TRUE(result.ok());
  auto factory_cb = result.value();
  Http::MockFilterChainFactoryCallbacks callbacks;

  EXPECT_CALL(callbacks, addStreamDecoderFilter(testing::_));
  EXPECT_CALL(callbacks, addStreamEncoderFilter(testing::_));
  factory_cb(callbacks);
}

TEST(DynamicModuleConfigFactory, LoadError) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  // Non existent module.
  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: something-not-exist
filter_name: foo
filter_config: bar
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to load dynamic module:"));

  // The init function envoy_dynamic_module_on_http_filter_config_new is not defined.
  const std::string yaml2 = R"EOF(
dynamic_module_config:
    name: no_http_config_new
filter_name: foo
filter_config: bar
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config2;
  TestUtility::loadFromYamlAndValidate(yaml2, proto_config2);

  auto result2 = factory.createFilterFactoryFromProto(proto_config2, "", context);
  EXPECT_FALSE(result2.ok());
  EXPECT_EQ(result2.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result2.status().message(),
              testing::HasSubstr(
                  "Failed to resolve symbol envoy_dynamic_module_on_http_filter_config_new"));

  // The destroy function envoy_dynamic_module_on_http_filter_config_destroy is not defined.
  const std::string yaml3 = R"EOF(
dynamic_module_config:
    name: no_http_config_destory
filter_name: foo
filter_config: bar
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config3;
  TestUtility::loadFromYamlAndValidate(yaml3, proto_config3);

  auto result3 = factory.createFilterFactoryFromProto(proto_config3, "", context);
  EXPECT_FALSE(result3.ok());
  EXPECT_EQ(result3.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result3.status().message(),
              testing::HasSubstr(
                  "Failed to resolve symbol envoy_dynamic_module_on_http_filter_config_destroy"));
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
