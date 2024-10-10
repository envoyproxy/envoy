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
  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
    object_file:
        filename: {}
    do_not_close: true
filter_name: foo
filter_config: bar
)EOF",
                                       testSharedObjectPath("no_op", "rust"));

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
    object_file:
        filename: something-not-exist.so
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

TEST(DynamicModuleConfigFactory, NotSupported) {
  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    object_file:
        inline_string: foo
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
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr(
                  "Only filename is supported as a data source of dynamic module object file"));
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
