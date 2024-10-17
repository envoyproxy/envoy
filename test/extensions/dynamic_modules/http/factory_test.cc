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
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
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
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
