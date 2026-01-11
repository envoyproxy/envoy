#include "envoy/extensions/bootstrap/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/bootstrap/dynamic_modules/factory.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class FactoryTestBase : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST(FactoryTest, Name) {
  DynamicModuleBootstrapExtensionFactory factory;
  EXPECT_EQ(factory.name(), "envoy.bootstrap.dynamic_modules");
}

TEST(FactoryTest, CreateEmptyConfigProto) {
  DynamicModuleBootstrapExtensionFactory factory;
  auto config = factory.createEmptyConfigProto();
  EXPECT_NE(config, nullptr);
}

TEST_F(FactoryTestBase, DynamicModuleLoadFail) {
  // Test that factory throws when dynamic module fails to load.
  DynamicModuleBootstrapExtensionFactory factory;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  proto_config.set_extension_name("test");

  EXPECT_THROW_WITH_REGEX(factory.createBootstrapExtension(proto_config, context_), EnvoyException,
                          "Failed to load dynamic module:.*");

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(FactoryTestBase, ExtensionConfigCreateFail) {
  // Test that factory throws when extension config creation fails.
  DynamicModuleBootstrapExtensionFactory factory;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("bootstrap_no_config_new");
  proto_config.set_extension_name("test");

  EXPECT_THROW_WITH_REGEX(factory.createBootstrapExtension(proto_config, context_), EnvoyException,
                          "Failed to create extension config:.*");

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(FactoryTestBase, InvalidExtensionConfig) {
  // Test that factory throws when extension_config Any message fails to parse.
  // This covers the config_or_error.ok() check in factory.cc.
  DynamicModuleBootstrapExtensionFactory factory;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("bootstrap_no_op");
  proto_config.set_extension_name("test");

  // Create an Any message that claims to be a StringValue but has invalid/corrupted data.
  // The type_url says it's a StringValue, but the value is not a valid protobuf encoding.
  auto* extension_config = proto_config.mutable_extension_config();
  extension_config->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  extension_config->set_value("invalid\xff\xfe protobuf data that cannot be parsed");

  EXPECT_THROW_WITH_REGEX(factory.createBootstrapExtension(proto_config, context_), EnvoyException,
                          "Failed to parse extension config:.*");

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
