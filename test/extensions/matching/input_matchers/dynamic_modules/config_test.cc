#include "envoy/registry/registry.h"

#include "source/extensions/matching/input_matchers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {
namespace {

class DynamicModuleInputMatcherFactoryTest : public testing::Test {
public:
  DynamicModuleInputMatcherFactoryTest() {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("matcher_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  DynamicModuleInputMatcherFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(DynamicModuleInputMatcherFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.matching.matchers.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleInputMatcherFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(
      nullptr,
      dynamic_cast<
          envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher*>(
          proto.get()));
}

TEST_F(DynamicModuleInputMatcherFactoryTest, ValidConfig) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: matcher_no_op
  do_not_close: true
matcher_name: test_matcher
)EOF";

  envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto factory_cb = factory_.createInputMatcherFactoryCb(proto_config, context_);
  EXPECT_NE(nullptr, factory_cb);
  auto matcher = factory_cb();
  EXPECT_NE(nullptr, matcher);
}

TEST_F(DynamicModuleInputMatcherFactoryTest, ValidConfigWithMatcherConfig) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: matcher_check_headers
  do_not_close: true
matcher_name: header_matcher
matcher_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: x-test-header
)EOF";

  envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto factory_cb = factory_.createInputMatcherFactoryCb(proto_config, context_);
  EXPECT_NE(nullptr, factory_cb);
  auto matcher = factory_cb();
  EXPECT_NE(nullptr, matcher);
}

TEST_F(DynamicModuleInputMatcherFactoryTest, InvalidModule) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: nonexistent_module
matcher_name: test_matcher
)EOF";

  envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_REGEX(factory_.createInputMatcherFactoryCb(proto_config, context_),
                          EnvoyException, "Failed to load.*");
}

TEST_F(DynamicModuleInputMatcherFactoryTest, MissingConfigNew) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: matcher_missing_config_new
  do_not_close: true
matcher_name: test_matcher
)EOF";

  envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_REGEX(factory_.createInputMatcherFactoryCb(proto_config, context_),
                          EnvoyException, "Failed to resolve symbol.*config_new");
}

TEST_F(DynamicModuleInputMatcherFactoryTest, MissingConfigDestroy) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: matcher_missing_config_destroy
  do_not_close: true
matcher_name: test_matcher
)EOF";

  envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_REGEX(factory_.createInputMatcherFactoryCb(proto_config, context_),
                          EnvoyException, "Failed to resolve symbol.*config_destroy");
}

TEST_F(DynamicModuleInputMatcherFactoryTest, MissingMatch) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: matcher_missing_match
  do_not_close: true
matcher_name: test_matcher
)EOF";

  envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_REGEX(factory_.createInputMatcherFactoryCb(proto_config, context_),
                          EnvoyException, "Failed to resolve symbol.*matcher_match");
}

TEST_F(DynamicModuleInputMatcherFactoryTest, ConfigNewReturnsNull) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: matcher_config_new_fail
  do_not_close: true
matcher_name: test_matcher
)EOF";

  envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_REGEX(factory_.createInputMatcherFactoryCb(proto_config, context_),
                          EnvoyException, "Failed to initialize dynamic module matcher config");
}

TEST_F(DynamicModuleInputMatcherFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<::Envoy::Matcher::InputMatcherFactory>::getFactory(
      "envoy.matching.matchers.dynamic_modules");
  EXPECT_NE(nullptr, factory);
}

} // namespace
} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
