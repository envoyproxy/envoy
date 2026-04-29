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

// Mirror of BasicRust against the Go SDK. Exercises the same lifecycle hooks
// (server_initialized / worker_thread_initialized / shutdown) and specifically validates
// that the shutdown completion callback actually reaches Envoy from Go — a bug previously
// existed where the trampoline discarded the callback, hanging Envoy teardown.
TEST_P(DynamicModulesBootstrapIntegrationTest, BasicGo) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "Bootstrap extension server initialized from Go!"},
           {"info", "Bootstrap extension worker thread initialized from Go!"}}),
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_integration_test"));

  // Verify the shutdown hook is called during server teardown. If the Go shutdown
  // completion fix regresses, the test_server_.reset() call will hang and time out.
  EXPECT_LOG_CONTAINS("info", "Bootstrap extension shutdown from Go!", { test_server_.reset(); });
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

// Mirror of StatsAccessRust against the Go SDK. The Go module emits the same set of log
// lines so the same expectations apply.
TEST_P(DynamicModulesBootstrapIntegrationTest, StatsAccessGo) {
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
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_stats_test"));
}

// This test verifies that the Rust bootstrap extension can register and resolve functions
// via the process-wide function registry.
TEST_P(DynamicModulesBootstrapIntegrationTest, FunctionRegistryRust) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap function registry test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_function_registry_test"));
}

// Mirror of FunctionRegistryRust against the Go SDK. The Go module exercises the
// register/get round-trip with sentinel pointers (Go can't directly produce C function
// pointers from Go funcs).
TEST_P(DynamicModulesBootstrapIntegrationTest, FunctionRegistryGo) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap function registry test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_function_registry_test"));
}

// This test verifies that the Rust bootstrap extension can register, retrieve, and overwrite
// shared data via the process-wide shared data registry.
TEST_P(DynamicModulesBootstrapIntegrationTest, SharedDataRegistryRust) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap shared data registry test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_shared_data_test"));
}

// Mirror of SharedDataRegistryRust against the Go SDK.
TEST_P(DynamicModulesBootstrapIntegrationTest, SharedDataRegistryGo) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap shared data registry test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_shared_data_test"));
}

// This test verifies that Envoy automatically registers an init target for every bootstrap
// extension and that the module can signal readiness to unblock startup.
TEST_P(DynamicModulesBootstrapIntegrationTest, InitTargetRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Init target signaled complete during config creation"},
                                  {"info", "Bootstrap init target test completed successfully!"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_init_target_test"));
}

// Mirror of InitTargetRust against the Go SDK.
TEST_P(DynamicModulesBootstrapIntegrationTest, InitTargetGo) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Init target signaled complete during config creation"},
                                  {"info", "Bootstrap init target test completed successfully!"}}),
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_init_target_test"));
}

// This test verifies that the Rust bootstrap extension timer API works correctly.
// Two timers are created during config_new, armed with short delays, and on_timer_fired uses the
// timer identity API to distinguish which timer fired. Init completes after both timers fire.
TEST_P(DynamicModulesBootstrapIntegrationTest, TimerRust) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap timer test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_timer_test"));
}

// Mirror of TimerRust against the Go SDK. Go uses per-timer onFire closures rather than
// an on_timer_fired hook, but the externally observable behavior is the same.
TEST_P(DynamicModulesBootstrapIntegrationTest, TimerGo) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap timer test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_timer_test"));
}

// This test verifies that the Rust bootstrap extension file watcher API works correctly.
// Two files are watched via separate add_file_watch calls. Three timed writes occur: file_a twice
// and file_b once. on_file_changed tracks per-path counts, and signals init complete only after
// file_a has been seen at least 2 times and file_b at least 1 time.
TEST_P(DynamicModulesBootstrapIntegrationTest, FileWatcherRust) {
  // Create two temporary files for the watcher to monitor.
  const std::string path_a =
      TestEnvironment::writeStringToFileForTest("file_watcher_test_a", "initial a");
  const std::string path_b =
      TestEnvironment::writeStringToFileForTest("file_watcher_test_b", "initial b");
  // Pass both paths separated by |.
  const std::string config = path_a + "|" + path_b;

  EXPECT_LOG_CONTAINS("info", "Bootstrap file watcher test completed successfully!",
                      initializeWithBootstrapExtension(
                          testDataDir("rust"), "bootstrap_file_watcher_test", "test", config));
}

// Mirror of FileWatcherRust against the Go SDK.
TEST_P(DynamicModulesBootstrapIntegrationTest, FileWatcherGo) {
  const std::string path_a =
      TestEnvironment::writeStringToFileForTest("file_watcher_test_go_a", "initial a");
  const std::string path_b =
      TestEnvironment::writeStringToFileForTest("file_watcher_test_go_b", "initial b");
  const std::string config = path_a + "|" + path_b;

  EXPECT_LOG_CONTAINS("info", "Bootstrap file watcher test completed successfully!",
                      initializeWithBootstrapExtension(
                          testDataDir("go"), "bootstrap_file_watcher_test", "test", config));
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

// Mirror of AdminHandlerRust against the Go SDK.
TEST_P(DynamicModulesBootstrapIntegrationTest, AdminHandlerGo) {
  EXPECT_LOG_CONTAINS(
      "info", "Admin handler registered: true",
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_admin_handler_test"));

  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/dynamic_module_admin_test",
                                         "", Http::CodecType::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("Hello from dynamic module admin handler!"));

  EXPECT_LOG_CONTAINS("info", "Admin request received: GET", {
    response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                  "/dynamic_module_admin_test?foo=bar", "",
                                                  Http::CodecType::HTTP1, version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  });
}

// This test verifies that the Rust bootstrap extension can receive cluster lifecycle events
// (add/update and removal) via the ClusterUpdateCallbacks mechanism.
TEST_P(DynamicModulesBootstrapIntegrationTest, ClusterLifecycleRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap cluster lifecycle test: server initialized"},
                                  {"info", "Cluster lifecycle enabled: true"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_cluster_lifecycle_test"));
}

// Mirror of ClusterLifecycleRust against the Go SDK.
TEST_P(DynamicModulesBootstrapIntegrationTest, ClusterLifecycleGo) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap cluster lifecycle test: server initialized"},
                                  {"info", "Cluster lifecycle enabled: true"}}),
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_cluster_lifecycle_test"));
}

// This test verifies that the Rust bootstrap extension can receive listener lifecycle events
// (add/update and removal) via the ListenerUpdateCallbacks mechanism.
TEST_P(DynamicModulesBootstrapIntegrationTest, ListenerLifecycleRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap listener lifecycle test: server initialized"},
                                  {"info", "Listener lifecycle enabled: true"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_listener_lifecycle_test"));
}

// Mirror of ListenerLifecycleRust against the Go SDK.
TEST_P(DynamicModulesBootstrapIntegrationTest, ListenerLifecycleGo) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap listener lifecycle test: server initialized"},
                                  {"info", "Listener lifecycle enabled: true"}}),
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_listener_lifecycle_test"));
}

// This test verifies that a bootstrap extension can register a function in the process-wide
// function registry and an HTTP filter in the same module can resolve and call it during request
// processing. The bootstrap extension asynchronously initializes a routing table and registers a
// lookup function. The HTTP filter resolves this function via get_function and uses it to route
// requests based on the x-target-service header.
TEST_P(DynamicModulesBootstrapIntegrationTest, FunctionRegistryCrossFilterRust) {
  const std::string module_dir = testDataDir("rust");
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", module_dir, 1);

  // Add the bootstrap extension that initializes the routing table and registers the lookup
  // function.
  const std::string bootstrap_yaml = R"EOF(
      name: envoy.bootstrap.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
        dynamic_module_config:
          name: bootstrap_http_combined_test
        extension_name: combined_test
        extension_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: test
    )EOF";
  config_helper_.addBootstrapExtension(bootstrap_yaml);

  // Add the HTTP filter from the same module that resolves the function from the registry.
  const std::string http_filter_yaml = R"EOF(
name: envoy.extensions.filters.http.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
  dynamic_module_config:
    name: bootstrap_http_combined_test
  filter_name: combined_filter
  filter_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: ""
)EOF";
  config_helper_.prependFilter(http_filter_yaml);

  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "bootstrap init signaled complete after async initialization"},
           {"info", "http filter config created (function resolution deferred to request time)"}}),
      HttpIntegrationTest::initialize());

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Case 1: Request with a known service should be routed with x-routed-to header.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-target-service", "service-a"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    // Verify the filter added the routing header via the function registry lookup.
    EXPECT_EQ("10.0.0.1:8080", upstream_request_->headers()
                                   .get(Http::LowerCaseString("x-routed-to"))[0]
                                   ->value()
                                   .getStringView());
  }

  // Case 2: Request with another known service.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-target-service", "service-b"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    EXPECT_EQ("10.0.0.2:9090", upstream_request_->headers()
                                   .get(Http::LowerCaseString("x-routed-to"))[0]
                                   ->value()
                                   .getStringView());
  }

  // Case 3: Request with an unknown service should get a 503 local reply.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-target-service", "unknown-service"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);
    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());
    EXPECT_EQ("503", response->headers().Status()->value().getStringView());
    EXPECT_EQ("service_not_onboarded", response->headers()
                                           .get(Http::LowerCaseString("x-error-reason"))[0]
                                           ->value()
                                           .getStringView());
  }

  // Case 4: Request without x-target-service header should pass through.
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    // No x-routed-to header should be present.
    EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("x-routed-to")).empty());
  }
}

// Mirror of FunctionRegistryCrossFilterRust against the Go SDK. The Go module registers
// the address of a package-level Go func var via sdk.RegisterFunction, and the HTTP
// filter resolves it via sdk.GetFunction and dereferences it back to a callable. Both
// extensions live in the same .so so the in-process function can be safely re-cast
// from unsafe.Pointer; the cross-language goal is to prove the Envoy registry round-trip
// preserves the pointer.
TEST_P(DynamicModulesBootstrapIntegrationTest, FunctionRegistryCrossFilterGo) {
  const std::string module_dir = testDataDir("go");
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", module_dir, 1);
  TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);

  const std::string bootstrap_yaml = R"EOF(
      name: envoy.bootstrap.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
        dynamic_module_config:
          name: bootstrap_http_combined_test
        extension_name: combined_test
        extension_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: test
    )EOF";
  config_helper_.addBootstrapExtension(bootstrap_yaml);

  const std::string http_filter_yaml = R"EOF(
name: envoy.extensions.filters.http.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
  dynamic_module_config:
    name: bootstrap_http_combined_test
  filter_name: combined_filter
  filter_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: ""
)EOF";
  config_helper_.prependFilter(http_filter_yaml);

  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "bootstrap init signaled complete after async initialization"},
           {"info", "http filter config created (function resolution deferred to request time)"}}),
      HttpIntegrationTest::initialize());

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Case 1: Known service routes successfully.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-target-service", "service-a"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    EXPECT_EQ("10.0.0.1:8080", upstream_request_->headers()
                                   .get(Http::LowerCaseString("x-routed-to"))[0]
                                   ->value()
                                   .getStringView());
  }

  // Case 2: Different known service.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-target-service", "service-b"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    EXPECT_EQ("10.0.0.2:9090", upstream_request_->headers()
                                   .get(Http::LowerCaseString("x-routed-to"))[0]
                                   ->value()
                                   .getStringView());
  }

  // Case 3: Unknown service gets a 503 local reply.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-target-service", "unknown-service"}};
    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("503", response->headers().Status()->value().getStringView());
    EXPECT_EQ("service_not_onboarded", response->headers()
                                           .get(Http::LowerCaseString("x-error-reason"))[0]
                                           ->value()
                                           .getStringView());
  }

  // Case 4: No x-target-service header — pass through, no routing header set.
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("x-routed-to")).empty());
  }
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
