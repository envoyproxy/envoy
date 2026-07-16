#include <filesystem>

#include "envoy/registry/registry.h"

#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/dynamic_modules/config.h"
#include "source/extensions/health_checkers/dynamic_modules/health_checker.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {
namespace {

// Builds a HealthCheck config whose custom_health_check points at the dynamic module checker.
std::string healthCheckYaml(const std::string& module_name, const std::string& checker_name,
                            bool with_config) {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.dynamic_modules.v3.DynamicModuleHealthCheck
        dynamic_module_config:
          name: )EOF" +
                     module_name +
                     R"EOF(
          do_not_close: true
        health_checker_name: )EOF" +
                     checker_name;
  if (with_config) {
    yaml += R"EOF(
        health_checker_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: healthy)EOF";
  }
  return yaml;
}

// Uses C test modules (which exercise symbol resolution without driving sessions).
class DynamicModuleHealthCheckerFactoryTest : public testing::Test {
public:
  DynamicModuleHealthCheckerFactoryTest() {
    const std::string so =
        Extensions::DynamicModules::testSharedObjectPath("health_checker_no_op", "c");
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               std::filesystem::path(so).parent_path().string(), 1);
  }

  DynamicModuleHealthCheckerFactory factory_;
};

TEST_F(DynamicModuleHealthCheckerFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.health_checkers.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleHealthCheckerFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr,
            dynamic_cast<
                envoy::extensions::health_checkers::dynamic_modules::v3::DynamicModuleHealthCheck*>(
                proto.get()));
}

TEST_F(DynamicModuleHealthCheckerFactoryTest, FactoryRegistration) {
  EXPECT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::CustomHealthCheckerFactory>::getFactory(
          "envoy.health_checkers.dynamic_modules"));
}

// Full factory success path: opaque config translation, module load, config-bytes decoding, and an
// in-module config that omits the optional on_timeout symbol.
TEST_F(DynamicModuleHealthCheckerFactoryTest, CreateValid) {
  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  const std::string yaml = healthCheckYaml("health_checker_no_op", "test", /*with_config=*/true);
  EXPECT_NE(
      nullptr,
      dynamic_cast<DynamicModuleHealthChecker*>(
          factory_.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST_F(DynamicModuleHealthCheckerFactoryTest, InvalidModule) {
  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  const std::string yaml = healthCheckYaml("nonexistent_module", "test", /*with_config=*/false);
  EXPECT_THROW_WITH_REGEX(
      factory_.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context),
      EnvoyException, "Failed to load dynamic module.*");
}

// Each missing required symbol is rejected at config load with a symbol-resolution error.
TEST_F(DynamicModuleHealthCheckerFactoryTest, MissingSymbols) {
  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  const std::pair<std::string, std::string> cases[] = {
      {"health_checker_missing_config_new", "config_new"},
      {"health_checker_missing_config_destroy", "config_destroy"},
      {"health_checker_missing_session_new", "session_new"},
      {"health_checker_missing_session_on_interval", "session_on_interval"},
      {"health_checker_missing_session_destroy", "session_destroy"},
  };
  for (const auto& [module_name, symbol] : cases) {
    const std::string yaml = healthCheckYaml(module_name, "test", /*with_config=*/false);
    EXPECT_THROW_WITH_REGEX(
        factory_.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context),
        EnvoyException, "Failed to resolve symbol.*" + symbol);
  }
}

// Uses the Rust test module, whose factory returns None for an unknown name so the in-module config
// creation fails.
TEST(DynamicModuleHealthCheckerFactoryRustTest, UnknownHealthCheckerNameRejected) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);
  DynamicModuleHealthCheckerFactory factory;
  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  const std::string yaml =
      healthCheckYaml("health_checker_integration_test", "unknown_checker", /*with_config=*/false);
  EXPECT_THROW_WITH_REGEX(
      factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context),
      EnvoyException, "Failed to create dynamic module health checker config.*");
}

} // namespace
} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
