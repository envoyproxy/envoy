#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class DynamicModulesBootstrapIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModulesBootstrapIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithBootstrapExtension(const std::string& module_dir,
                                        const std::string& module_name = "test",
                                        const std::string& extension_name = "test",
                                        const std::string& extension_config = "test_config") {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", module_dir, 1);
    const std::string yaml = fmt::format(R"EOF(
      name: envoy.bootstrap.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
        dynamic_module_config:
          name: {}
        extension_name: {}
        extension_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: {}
    )EOF",
                                         module_name, extension_name, extension_config);

    config_helper_.addBootstrapExtension(yaml);
    HttpIntegrationTest::initialize();
  }

  std::string testDataDir(const std::string& subdir) {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/" + subdir);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesBootstrapIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesBootstrapIntegrationTest, BasicC) {
  initializeWithBootstrapExtension(testDataDir("c"), "bootstrap_no_op");
}

// This test verifies that the Rust bootstrap extension can use the common logging callbacks.
// The integration test module logs messages during on_server_initialized and
// on_worker_thread_initialized hooks.
TEST_P(DynamicModulesBootstrapIntegrationTest, BasicRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "Bootstrap extension server initialized from Rust!"},
           {"info", "Bootstrap extension worker thread initialized from Rust!"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_integration_test"));
}

// This test verifies that the Rust bootstrap extension can access stats from the stats store.
TEST_P(DynamicModulesBootstrapIntegrationTest, StatsAccessRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Correctly returned None for non-existent counter"},
                                  {"info", "Correctly returned None for non-existent gauge"},
                                  {"info", "Correctly returned None for non-existent histogram"},
                                  {"info", "Bootstrap stats access test completed successfully!"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_stats_test"));
}

// This test verifies that the Rust bootstrap extension can register and resolve functions
// via the process-wide function registry.
TEST_P(DynamicModulesBootstrapIntegrationTest, FunctionRegistryRust) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap function registry test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_function_registry_test"));
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
