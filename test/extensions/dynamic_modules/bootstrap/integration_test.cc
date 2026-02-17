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
// The integration test module logs messages during on_server_initialized,
// on_worker_thread_initialized, and on_shutdown hooks.
TEST_P(DynamicModulesBootstrapIntegrationTest, BasicRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "Bootstrap extension server initialized from Rust!"},
           {"info", "Bootstrap extension worker thread initialized from Rust!"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_integration_test"));

  // Verify the shutdown hook is called during server teardown.
  EXPECT_LOG_CONTAINS("info", "Bootstrap extension shutdown from Rust!", { test_server_.reset(); });
}

// This test verifies that the Rust bootstrap extension can access stats from the stats store
// and define/update its own metrics (counters, gauges, histograms).
TEST_P(DynamicModulesBootstrapIntegrationTest, StatsAccessRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "Counter incremented to expected value of 5"},
           {"info", "Gauge set to expected value of 80"},
           {"info", "Histogram values recorded successfully"},
           {"info", "Counter vec incremented successfully"},
           {"info", "Gauge vec manipulated successfully"},
           {"info", "Histogram vec recorded successfully"},
           {"info", "Bootstrap metrics definition and update test completed successfully!"},
           {"info", "Correctly returned None for non-existent counter"},
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

// This test verifies that Envoy automatically registers an init target for every bootstrap
// extension and that the module can signal readiness to unblock startup.
TEST_P(DynamicModulesBootstrapIntegrationTest, InitTargetRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Init target signaled complete during config creation"},
                                  {"info", "Bootstrap init target test completed successfully!"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_init_target_test"));
}

// This test verifies that the Rust bootstrap extension timer API works correctly.
// A timer is created during config_new, armed with a short delay, and on_timer_fired logs success.
TEST_P(DynamicModulesBootstrapIntegrationTest, TimerRust) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap timer test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_timer_test"));
}

// This test verifies that the Rust bootstrap extension can register a custom admin HTTP endpoint
// and respond to admin requests.
TEST_P(DynamicModulesBootstrapIntegrationTest, AdminHandlerRust) {
  EXPECT_LOG_CONTAINS(
      "info", "Admin handler registered: true",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_admin_handler_test"));

  // Make an admin request to the registered endpoint.
  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/dynamic_module_admin_test",
                                         "", Http::CodecType::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Hello from dynamic module admin handler!"));

  // Verify the admin request was logged.
  EXPECT_LOG_CONTAINS("info", "Admin request received: GET", {
    response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                  "/dynamic_module_admin_test?foo=bar", "",
                                                  Http::CodecType::HTTP1, version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  });
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
