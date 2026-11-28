#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "source/common/common/base64.h"
#include "source/common/config/resource_name.h"
#include "source/common/runtime/runtime_features.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
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

  void TearDown() override { cleanUpXdsConnection(); }

  void initializeWithBootstrapExtension(const std::string& module_dir,
                                        const std::string& module_name = "test",
                                        const std::string& extension_name = "test",
                                        const std::string& extension_config = "test_config",
                                        bool do_not_close = false) {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", module_dir, 1);
    // Only emit do_not_close when requested so the generated config is unchanged for modules that
    // do not need to be pinned in memory.
    const std::string do_not_close_config = do_not_close ? "\n          do_not_close: true" : "";
    const std::string yaml =
        fmt::format(R"EOF(
      name: envoy.bootstrap.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
        dynamic_module_config:
          name: {}{}
        extension_name: {}
        extension_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: {}
    )EOF",
                    module_name, do_not_close_config, extension_name, extension_config);

    config_helper_.addBootstrapExtension(yaml);
    HttpIntegrationTest::initialize();
  }

  std::string testDataDir(const std::string& subdir) {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/" + subdir);
  }

  void testXdsUnsubscriptions(Grpc::SotwOrDelta mode) {
    use_lds_ = false;
    defer_listener_finalization_ = true;
    sotw_or_delta_ = mode;
    const bool delta = mode == Grpc::SotwOrDelta::Delta || mode == Grpc::SotwOrDelta::UnifiedDelta;
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      mode == Grpc::SotwOrDelta::UnifiedSotw ||
                                              mode == Grpc::SotwOrDelta::UnifiedDelta
                                          ? "true"
                                          : "false");
    setUpstreamProtocol(Http::CodecType::HTTP2);
    config_helper_.addConfigModifier([delta](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* dynamic_resources = bootstrap.mutable_dynamic_resources();
      auto* ads = dynamic_resources->mutable_ads_config();
      ads->set_api_type(delta ? envoy::config::core::v3::ApiConfigSource::DELTA_GRPC
                              : envoy::config::core::v3::ApiConfigSource::GRPC);
      ads->set_transport_api_version(envoy::config::core::v3::V3);
      ads->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("cluster_0");
      auto* cds = dynamic_resources->mutable_cds_config();
      cds->mutable_ads();
      cds->set_resource_api_version(envoy::config::core::v3::V3);
    });
    initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_xds_test");
    registerTestServerPorts({});
    EXPECT_EQ(writerModuleRequest("/xds", "enable", ""), "");

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_));
    ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
    xds_stream_->startGrpcStream();
    const auto cluster_type = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
    const auto endpoint_type =
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
    ASSERT_TRUE(compareDiscoveryRequest(cluster_type, "", {}, {}, {}, true));
    auto cluster = ConfigHelper::buildCluster("eds_cluster");
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cluster_type, {cluster}, {cluster},
                                                               {}, "1");
    ASSERT_TRUE(
        compareDiscoveryRequest(endpoint_type, "", {"eds_cluster"}, {"eds_cluster"}, {}, true));
    auto endpoints = ConfigHelper::buildClusterLoadAssignment(
        "eds_cluster", Network::Test::getLoopbackAddressString(version_),
        fake_upstreams_[0]->localAddress()->ip()->port());
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        endpoint_type, {endpoints}, {endpoints}, {}, "1");
    ASSERT_TRUE(compareDiscoveryRequest(cluster_type, "1", {}, {}, {}, true));
    ASSERT_TRUE(compareDiscoveryRequest(endpoint_type, "1", {"eds_cluster"}, {}, {}, true));
    test_server_->waitUntilListenersReady();
    const auto updates = writerModuleRequest("/xds", "", "");
    const auto response_version = delta ? "system_version_info_this_is_a_test" : "1";
    EXPECT_THAT(updates, testing::HasSubstr(absl::StrCat("Accepted: ", cluster_type, ": ",
                                                         response_version, ": nonce")));
    EXPECT_THAT(updates, testing::HasSubstr(absl::StrCat("Accepted: ", endpoint_type, ": ",
                                                         response_version, ": nonce")));

    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cluster_type, {}, {},
                                                               {"eds_cluster"}, "2");
    ASSERT_TRUE(compareDiscoveryRequest(endpoint_type, "1", {}, {}, {"eds_cluster"}, true));
    ASSERT_TRUE(compareDiscoveryRequest(cluster_type, "2", {}, {}, {}, true));
    test_server_->waitForCounter("cluster_manager.cluster_removed", testing::Eq(1));
    EXPECT_THAT(
        writerModuleRequest("/xds", "", ""),
        testing::HasSubstr(absl::StrCat("Unsubscribed: ", endpoint_type, ": eds_cluster\n")));
  }

  // Starts Envoy with a Rust writer test module, which gets the serialized startup resource as its
  // configuration.
  void initializeWithWriterModule(absl::string_view module_name,
                                  const std::string& startup_resource) {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir("rust"), 1);
    config_helper_.addBootstrapExtension(
        fmt::format(R"EOF(
      name: envoy.bootstrap.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
        dynamic_module_config:
          name: {}
        extension_name: writer
        extension_config:
          "@type": type.googleapis.com/google.protobuf.BytesValue
          value: "{}"
    )EOF",
                    module_name, Base64::encode(startup_resource.data(), startup_resource.size())));
    HttpIntegrationTest::initialize();
  }

  // Sends an action to an admin endpoint of a writer test module.
  std::string writerModuleRequest(absl::string_view path, absl::string_view action,
                                  const std::string& body) {
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "POST", absl::StrCat(path, "?", action), body, Http::CodecType::HTTP1,
        version_);
    EXPECT_EQ("200", response->headers().getStatusValue());
    return response->body();
  }

  // Starts Envoy with the bootstrap_cluster_writer_test module, which writes startup_cluster at
  // startup when it is not empty.
  void initializeWithClusterWriter(const std::string& startup_cluster) {
    initializeWithWriterModule("bootstrap_cluster_writer_test", startup_cluster);
  }

  // Sends an action to the admin endpoint of the bootstrap_cluster_writer_test module.
  std::string clusterWriterRequest(absl::string_view action, const std::string& body) {
    return writerModuleRequest("/cluster_writer", action, body);
  }

  // Starts Envoy with the bootstrap_writer_test module, which writes startup_listener
  // before it signals that its initialization is complete when it is not empty.
  void initializeWithWriter(const std::string& startup_listener) {
    initializeWithWriterModule("bootstrap_writer_test", startup_listener);
  }

  // Sends an action to the admin endpoint of the bootstrap_writer_test module.
  std::string writerRequest(absl::string_view action, const std::string& body) {
    return writerModuleRequest("/writer", action, body);
  }

  // Builds a filter chain that responds to every request with the status.
  envoy::config::listener::v3::FilterChain directResponseFilterChain(const std::string& name,
                                                                     uint32_t status = 200) {
    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(fmt::format(R"EOF(
name: {}
filters:
- name: envoy.filters.network.http_connection_manager
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: {}
    route_config:
      virtual_hosts:
      - name: direct_response
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          direct_response: {{ status: {} }}
    http_filters:
    - name: envoy.filters.http.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF",
                                          name, name, status),
                              filter_chain);
    return filter_chain;
  }

  // Builds a listener that finds its filter chain by name without an FCDS config source.
  envoy::config::listener::v3::Listener fcdsListener(const std::string& name,
                                                     const std::string& filter_chain_name) {
    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(fmt::format(R"EOF(
name: {}
address:
  socket_address:
    address: {}
    port_value: 0
fcds_config: {{}}
filter_chain_matcher:
  on_no_match:
    action:
      name: filter-chain-name
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: {}
)EOF",
                                          name, Network::Test::getLoopbackAddressString(version_),
                                          filter_chain_name),
                              listener);
    return listener;
  }

  // Builds a listener that responds to every request with a 200.
  envoy::config::listener::v3::Listener directResponseListener(const std::string& name) {
    envoy::config::listener::v3::Listener listener;
    listener.set_name(name);
    envoy::config::core::v3::SocketAddress* socket_address =
        listener.mutable_address()->mutable_socket_address();
    socket_address->set_address(Network::Test::getLoopbackAddressString(version_));
    socket_address->set_port_value(0);
    *listener.add_filter_chains() = directResponseFilterChain(name);
    return listener;
  }

  // Runs a worker event of the bootstrap_writer_test module. The admin request runs in a
  // later loop iteration than the writes it follows, so every worker runs the event after it
  // applied them.
  void runOnAllWorkers(uint64_t event_id) {
    EXPECT_EQ("true", writerRequest(absl::StrCat("run_on_all_workers=", event_id), ""));
    test_server_->waitForGauge("dynamicmodulescustom.last_done_event_id", testing::Eq(event_id));
  }

  // Sends a request to the listener that was registered as the port and checks its status.
  void expectDirectResponse(const std::string& port_name, absl::string_view status = "200") {
    codec_client_ = makeHttpConnection(lookupPort(port_name));
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ(status, response->headers().getStatusValue());
    codec_client_->close();
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

TEST_P(DynamicModulesBootstrapIntegrationTest, BasicGo) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Go bootstrap scheduled initialization"},
                                  {"info", "Go bootstrap initialization complete"},
                                  {"info", "Go bootstrap server initialized"},
                                  {"info", "Go bootstrap workers initialized"}}),
      initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_integration_test", "test",
                                       "normal", true));
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Go bootstrap asynchronous shutdown"},
                                  {"info", "Go bootstrap instance destroyed"},
                                  {"info", "Go bootstrap config destroyed"}}),
      { test_server_.reset(); });
}

TEST_P(DynamicModulesBootstrapIntegrationTest, DefaultLifecycleGo) {
  initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_integration_test", "test",
                                   "default", true);
  test_server_.reset();
}

TEST_P(DynamicModulesBootstrapIntegrationTest, ShutdownPanicGo) {
  initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_integration_test", "test",
                                   "shutdown_panic", true);
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"error", "bootstrap shutdown panic"}, {"info", "Go bootstrap config destroyed"}}),
      { test_server_.reset(); });
}

TEST_P(DynamicModulesBootstrapIntegrationTest, DestroyPanicGo) {
  initializeWithBootstrapExtension(testDataDir("go"), "bootstrap_integration_test", "test",
                                   "destroy_panic", true);
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"error", "bootstrap instance destroy panic"},
                                  {"error", "bootstrap config destroy panic"}}),
      { test_server_.reset(); });
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

// This test verifies that a panic inside a stats iterator visitor is caught by the SDK trampoline
// so Envoy stays up instead of aborting, and iteration returns to the module.
TEST_P(DynamicModulesBootstrapIntegrationTest, IteratePanicRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"error", "bootstrap_extension_iterate_counters: caught panic at FFI boundary"},
           {"error", "bootstrap_extension_iterate_gauges: caught panic at FFI boundary"},
           {"info", "Survived panic inside iterate_counters visitor"},
           {"info", "Survived panic inside iterate_gauges visitor"},
           {"info", "Bootstrap iterate panic test completed successfully!"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_iterate_panic_test"));
}

// This test verifies that the Rust bootstrap extension can register and resolve functions
// via the process-wide function registry.
TEST_P(DynamicModulesBootstrapIntegrationTest, FunctionRegistryRust) {
  // The module registers process-wide function pointers, so it must be pinned with do_not_close.
  // Otherwise the module would be unloaded between the IPv4 and IPv6 runs, and the registry would
  // hand back a dangling pointer on the second run, crashing when it is called.
  EXPECT_LOG_CONTAINS("info", "Bootstrap function registry test completed successfully!",
                      initializeWithBootstrapExtension(testDataDir("rust"),
                                                       "bootstrap_function_registry_test", "test",
                                                       "test_config", /*do_not_close=*/true));
}

// This test verifies that the Rust bootstrap extension can register, retrieve, and overwrite
// shared data via the process-wide shared data registry.
TEST_P(DynamicModulesBootstrapIntegrationTest, SharedDataRegistryRust) {
  // The shared data registry overwrites existing entries, so each parameterized run re-registers a
  // fresh pointer before reading it; the module therefore needs no do_not_close.
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap shared data registry test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_shared_data_test"));
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
// Two timers are created during config_new, armed with short delays, and on_timer_fired uses the
// timer identity API to distinguish which timer fired. Init completes after both timers fire.
TEST_P(DynamicModulesBootstrapIntegrationTest, TimerRust) {
  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap timer test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_timer_test"));
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

// This test verifies that the Rust bootstrap extension can register a custom admin HTTP endpoint
// and respond to admin requests.
TEST_P(DynamicModulesBootstrapIntegrationTest, AdminHandlerRust) {
  DISABLE_IF_ADMIN_DISABLED;
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

// This test verifies that the Rust bootstrap extension can receive cluster lifecycle events
// (add/update and removal) via the ClusterUpdateCallbacks mechanism.
TEST_P(DynamicModulesBootstrapIntegrationTest, ClusterLifecycleRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap cluster lifecycle test: server initialized"},
                                  {"info", "Cluster lifecycle enabled: true"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_cluster_lifecycle_test"));
}

TEST_P(DynamicModulesBootstrapIntegrationTest, EcdsConfigRust) {
  DISABLE_IF_ADMIN_DISABLED;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir("rust"), 1);
  for (const auto* path : {"/ecds_a", "/ecds_b"}) {
    config_helper_.addBootstrapExtension(fmt::format(R"EOF(
name: envoy.bootstrap.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
  dynamic_module_config:
    name: bootstrap_ecds_test
  extension_name: test
  extension_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: {}
)EOF",
                                                     path));
  }
  HttpIntegrationTest::initialize();

  const auto response = [](absl::string_view version, absl::string_view value) {
    return fmt::format(R"EOF(
version_info: "{}"
resources:
- "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
  name: test-config
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: "{}"
)EOF",
                       version, value);
  };
  const std::string config_path =
      TestEnvironment::writeStringToFileForTest("bootstrap_ecds.yaml", response("", "enabled"));
  envoy::config::core::v3::ExtensionConfigSource source;
  source.mutable_config_source()->mutable_path_config_source()->set_path(config_path);
  source.mutable_config_source()->set_resource_api_version(envoy::config::core::v3::V3);
  source.add_type_urls("type.googleapis.com/google.protobuf.StringValue");
  const auto source_json = MessageUtil::getJsonStringFromMessageOrError(source);
  EXPECT_EQ(writerModuleRequest("/ecds_a", "subscribe", source_json), "enabled");
  EXPECT_EQ(writerModuleRequest("/ecds_b", "subscribe", source_json), "enabled");
  EXPECT_EQ(writerModuleRequest("/ecds_a", "version", ""), "");

  const auto replace_config = [&](const std::string& contents) {
    TestEnvironment::renameFile(
        TestEnvironment::writeStringToFileForTest("bootstrap_ecds_update.yaml", contents),
        config_path);
  };
  const auto expect_config = [&](absl::string_view expected) {
    EXPECT_EQ(writerModuleRequest("/ecds_a", "", ""), expected);
    EXPECT_EQ(writerModuleRequest("/ecds_b", "", ""), expected);
  };

  replace_config(response("2", "disabled"));
  test_server_->waitForCounter(
      "extension_config_discovery.bootstrap.test-config.test.update_success", testing::Ge(4));
  expect_config("disabled");

  replace_config(response("3", "rejected"));
  test_server_->waitForCounter(
      "extension_config_discovery.bootstrap.test-config.test.update_rejected", testing::Ge(2));
  expect_config("disabled");

  replace_config("version_info: '4'\nresources: []\n");
  test_server_->waitForCounter(
      "extension_config_discovery.bootstrap.test-config.test.update_success", testing::Ge(6));
  expect_config("disabled");

  replace_config(response("5", ""));
  test_server_->waitForCounter(
      "extension_config_discovery.bootstrap.test-config.test.update_success", testing::Ge(8));
  expect_config("");
}

class BootstrapEcdsIntegrationTest : public Event::TestUsingSimulatedTime,
                                     public DynamicModulesBootstrapIntegrationTest {
public:
  void configureAds(bool unified_mux, envoy::config::core::v3::ApiConfigSource::ApiType api_type) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      unified_mux ? "true" : "false");
    // the stats mode is selected before runtime overrides are loaded
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags", true);
    config_helper_.addConfigModifier(
        [api_type](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ads = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
          ads->set_api_type(api_type);
          ads->set_transport_api_version(envoy::config::core::v3::V3);
          ads->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("cluster_0");
        });
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void testAdsTtlRemoval(bool unified_mux) {
    DISABLE_IF_ADMIN_DISABLED;
    configureAds(unified_mux, envoy::config::core::v3::ApiConfigSource::GRPC);
    config_helper_.addBootstrapExtension(R"EOF(
name: envoy.bootstrap.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
  dynamic_module_config:
    name: bootstrap_ecds_test
  extension_name: second
  extension_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: /ecds_second
)EOF");
    initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_ecds_test", "test", "/ecds");

    const std::string source =
        R"({"config_source":{"ads":{},"resource_api_version":"V3"},"type_urls":["type.googleapis.com/google.protobuf.StringValue"]})";
    EXPECT_EQ(writerModuleRequest("/ecds", "subscribe", source), "removed");
    FakeHttpConnectionPtr connection;
    FakeStreamPtr stream;
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, connection));
    ASSERT_TRUE(connection->waitForNewStream(*dispatcher_, stream));
    stream->startGrpcStream();
    envoy::service::discovery::v3::DiscoveryRequest request;
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_THAT(request.resource_names(), testing::ElementsAre("test-config"));

    envoy::config::core::v3::TypedExtensionConfig config;
    config.set_name("test-config");
    Protobuf::StringValue value;
    value.set_value("keep-on-removal");
    MessageUtil::packFrom(*config.mutable_typed_config(), value);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name("test-config");
    resource.set_version("1");
    resource.mutable_ttl()->set_seconds(1);
    MessageUtil::packFrom(*resource.mutable_resource(), config);
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info("1");
    response.set_nonce("1");
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    MessageUtil::packFrom(*response.add_resources(), resource);
    stream->sendGrpcMessage(response);
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_FALSE(request.has_error_detail());
    EXPECT_EQ(writerModuleRequest("/ecds", "", ""), "keep-on-removal");
    auto counter = test_server_->counter(
        "extension_config_discovery.bootstrap.test-config.test.update_success");
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->tagExtractedName(),
              "extension_config_discovery.bootstrap.test-config.update_success");
    EXPECT_THAT(counter->tags(), testing::Contains(Stats::Tag{"config_name", "test"}));

    auto duplicate =
        IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST", "/ecds_second?subscribe",
                                           source, Http::CodecType::HTTP1, version_);
    EXPECT_EQ(duplicate->headers().getStatusValue(), "400");
    EXPECT_EQ(duplicate->body(), "subscription failed");

    resource.clear_resource();
    response.clear_resources();
    response.set_nonce("heartbeat");
    MessageUtil::packFrom(*response.add_resources(), resource);
    EXPECT_LOG_NOT_CONTAINS("warn", "rejected ECDS removal", {
      stream->sendGrpcMessage(response);
      ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
      EXPECT_FALSE(request.has_error_detail());
      EXPECT_EQ(writerModuleRequest("/ecds", "", ""), "keep-on-removal");
    });

    EXPECT_LOG_CONTAINS("warn", "rejected ECDS removal for test-config", {
      timeSystem().advanceTimeWait(std::chrono::seconds(2));
      EXPECT_EQ(writerModuleRequest("/ecds", "", ""), "keep-on-removal");
    });
    ASSERT_TRUE(connection->close());
    ASSERT_TRUE(connection->waitForDisconnect());
  }

  void testDeltaAds(bool unified_mux) {
    DISABLE_IF_ADMIN_DISABLED;
    configureAds(unified_mux, envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_ecds_test", "test", "/ecds");
    EXPECT_EQ(
        writerModuleRequest(
            "/ecds", "subscribe",
            R"({"config_source":{"ads":{},"resource_api_version":"V3"},"type_urls":["type.googleapis.com/google.protobuf.StringValue"]})"),
        "removed");
    FakeHttpConnectionPtr connection;
    FakeStreamPtr stream;
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, connection));
    ASSERT_TRUE(connection->waitForNewStream(*dispatcher_, stream));
    stream->startGrpcStream();
    envoy::service::discovery::v3::DeltaDiscoveryRequest request;
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_THAT(request.resource_names_subscribe(), testing::ElementsAre("test-config"));

    envoy::service::discovery::v3::DeltaDiscoveryResponse response;
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    response.set_nonce("missing");
    response.add_removed_resources("test-config");
    stream->sendGrpcMessage(response);
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_FALSE(request.has_error_detail());
    EXPECT_EQ(writerModuleRequest("/ecds", "", ""), "removed");

    response.clear_removed_resources();
    response.set_nonce("accepted");
    response.set_system_version_info("system-v1");
    auto* resource = response.add_resources();
    resource->set_name("test-config");
    resource->set_version("resource-v1");
    envoy::config::core::v3::TypedExtensionConfig config;
    config.set_name("test-config");
    Protobuf::StringValue value;
    value.set_value("enabled");
    MessageUtil::packFrom(*config.mutable_typed_config(), value);
    MessageUtil::packFrom(*resource->mutable_resource(), config);
    stream->sendGrpcMessage(response);
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_FALSE(request.has_error_detail());
    EXPECT_EQ(writerModuleRequest("/ecds", "", ""), "enabled");
    EXPECT_EQ(writerModuleRequest("/ecds", "version", ""), "resource-v1");

    response.set_nonce("rejected");
    response.set_system_version_info("system-v2");
    resource->set_version("resource-v2");
    value.set_value("rejected");
    MessageUtil::packFrom(*config.mutable_typed_config(), value);
    MessageUtil::packFrom(*resource->mutable_resource(), config);
    stream->sendGrpcMessage(response);
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_TRUE(request.has_error_detail());
    EXPECT_EQ(request.response_nonce(), "rejected");
    EXPECT_EQ(writerModuleRequest("/ecds", "", ""), "enabled");
    EXPECT_EQ(writerModuleRequest("/ecds", "version", ""), "resource-v1");

    response.clear_resources();
    response.set_nonce("removed");
    response.add_removed_resources("test-config");
    stream->sendGrpcMessage(response);
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_FALSE(request.has_error_detail());
    EXPECT_EQ(writerModuleRequest("/ecds", "", ""), "removed");
    ASSERT_TRUE(connection->close());
    ASSERT_TRUE(connection->waitForDisconnect());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, BootstrapEcdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(BootstrapEcdsIntegrationTest, AdsTtlRemovalRust) { testAdsTtlRemoval(false); }

TEST_P(BootstrapEcdsIntegrationTest, UnifiedAdsTtlRemovalRust) { testAdsTtlRemoval(true); }

TEST_P(BootstrapEcdsIntegrationTest, DeltaAdsRust) { testDeltaAds(false); }

TEST_P(BootstrapEcdsIntegrationTest, UnifiedDeltaAdsRust) { testDeltaAds(true); }

// This test verifies that the Rust bootstrap extension can receive listener lifecycle events
// (add/update and removal) via the ListenerUpdateCallbacks mechanism.
TEST_P(DynamicModulesBootstrapIntegrationTest, ListenerLifecycleRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap listener lifecycle test: server initialized"},
                                  {"info", "Listener lifecycle enabled: true"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_listener_lifecycle_test"));
}

TEST_P(DynamicModulesBootstrapIntegrationTest, XdsOutcomesRust) {
  DISABLE_IF_ADMIN_DISABLED;
  use_lds_ = false;
  defer_listener_finalization_ = true;
  setUpstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* dynamic_resources = bootstrap.mutable_dynamic_resources();
    auto* ads = dynamic_resources->mutable_ads_config();
    ads->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    ads->set_transport_api_version(envoy::config::core::v3::V3);
    ads->set_set_node_on_first_message_only(true);
    ads->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("cluster_0");
    auto* cds = dynamic_resources->mutable_cds_config();
    cds->mutable_ads();
    cds->set_resource_api_version(envoy::config::core::v3::V3);
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_xds_test");
  registerTestServerPorts({});
  EXPECT_EQ(writerModuleRequest("/xds", "enable", ""), "");

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_));
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
  xds_stream_->startGrpcStream();
  const auto type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  ASSERT_TRUE(compareSotwDiscoveryRequest(type_url, "", {}, true));
  sendSotwDiscoveryResponse<envoy::config::cluster::v3::Cluster>(type_url, {}, "1");
  envoy::service::discovery::v3::DiscoveryRequest ack;
  ASSERT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, ack));
  EXPECT_EQ(ack.type_url(), type_url);
  EXPECT_EQ(ack.version_info(), "1");
  EXPECT_EQ(ack.error_detail().code(), Grpc::Status::WellKnownGrpcStatus::Ok);
  test_server_->waitUntilListenersReady();
  const auto accepted = fmt::format("Accepted: {}: 1: {}\n", type_url, ack.response_nonce());
  EXPECT_EQ(writerModuleRequest("/xds", "", ""), accepted);

  sendSotwDiscoveryResponse<envoy::config::listener::v3::Listener>(
      type_url, {envoy::config::listener::v3::Listener()}, "2");
  envoy::service::discovery::v3::DiscoveryRequest nack;
  ASSERT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, nack));
  EXPECT_EQ(nack.type_url(), type_url);
  EXPECT_EQ(nack.version_info(), "1");
  ASSERT_FALSE(nack.error_detail().message().empty());
  const auto rejected =
      absl::StrCat(accepted, "Rejected: ", type_url, ": 2: ", nack.response_nonce(), ": ",
                   nack.error_detail().message(), "\n");
  EXPECT_EQ(writerModuleRequest("/xds", "", ""), rejected);

  sendSotwDiscoveryResponse<envoy::config::cluster::v3::Cluster>(type_url, {}, "3");
  ASSERT_TRUE(compareSotwDiscoveryRequest(type_url, "3", {}));
  EXPECT_EQ(writerModuleRequest("/xds", "", ""), rejected);
}

TEST_P(DynamicModulesBootstrapIntegrationTest, XdsUnsubscriptionsSotwRust) {
  DISABLE_IF_ADMIN_DISABLED;
  testXdsUnsubscriptions(Grpc::SotwOrDelta::Sotw);
}

TEST_P(DynamicModulesBootstrapIntegrationTest, XdsUnsubscriptionsDeltaRust) {
  DISABLE_IF_ADMIN_DISABLED;
  testXdsUnsubscriptions(Grpc::SotwOrDelta::Delta);
}

TEST_P(DynamicModulesBootstrapIntegrationTest, XdsUnsubscriptionsUnifiedSotwRust) {
  DISABLE_IF_ADMIN_DISABLED;
  testXdsUnsubscriptions(Grpc::SotwOrDelta::UnifiedSotw);
}

TEST_P(DynamicModulesBootstrapIntegrationTest, XdsUnsubscriptionsUnifiedDeltaRust) {
  DISABLE_IF_ADMIN_DISABLED;
  testXdsUnsubscriptions(Grpc::SotwOrDelta::UnifiedDelta);
}

// This test verifies that a Rust bootstrap extension is told about every change of the runtime keys
// it watches, including a runtime feature flag, with the new value, and is not told about the
// values present when it started watching, about unchanged values or about unwatched keys.
TEST_P(DynamicModulesBootstrapIntegrationTest, RuntimeUpdateRust) {
  DISABLE_IF_ADMIN_DISABLED;
  config_helper_.addRuntimeOverride("dynamic_modules_test.number", "1");
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "Runtime update enabled for dynamic_modules_test.number: true"},
           {"info", "Runtime update enabled for dynamic_modules_test.ratio: true"},
           {"info", "Runtime update enabled for dynamic_modules_test.mapping: true"},
           {"info",
            "Runtime update enabled for envoy.reloadable_features.test_feature_true: true"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_runtime_update_test"));

  auto runtime_modify = [this](absl::string_view query) {
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "POST", absl::StrCat("/runtime_modify?", query), "",
        Http::CodecType::HTTP1, version_);
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  };

  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "Runtime update: dynamic_modules_test.number=Int(42), getter agrees: true"},
           {"info", "Runtime update: dynamic_modules_test.ratio=Number(0.25), getter agrees: true"},
           {"info", "Runtime update: dynamic_modules_test.mapping=String(\"80:15100,8080:15101\"), "
                    "getter agrees: true"},
           {"info", "Runtime update: envoy.reloadable_features.test_feature_true=Bool(false), "
                    "getter agrees: true"}}),
      runtime_modify(
          "dynamic_modules_test.number=42&dynamic_modules_test.ratio=0.25&dynamic_modules_test."
          "mapping=80:15100,8080:15101&envoy.reloadable_features.test_feature_true=false&"
          "dynamic_modules_test.unwatched=1"));

  EXPECT_LOG_NOT_CONTAINS(
      "info", "Runtime update:",
      runtime_modify("dynamic_modules_test.number=42&dynamic_modules_test.unwatched=2"));

  // Removing the admin override of a key falls back to the lower layer, and removing the only
  // value of a key reports a removal.
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"info", "Runtime update: dynamic_modules_test.number=Int(1), getter agrees: true"},
           {"info", "Runtime update: dynamic_modules_test.ratio removed"},
           {"info", "Runtime update: dynamic_modules_test.mapping removed"},
           {"info", "Runtime update: envoy.reloadable_features.test_feature_true removed"}}),
      runtime_modify(
          "dynamic_modules_test.number=&dynamic_modules_test.ratio=&dynamic_modules_test."
          "mapping=&envoy.reloadable_features.test_feature_true="));
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
  // function. As in FunctionRegistryRust, the module must be pinned with do_not_close so the
  // registered function pointer stays valid across the IPv4 and IPv6 runs. Because do_not_close
  // only takes effect on the first load of the shared module, both the bootstrap and the HTTP
  // filter below set it to keep the module resident regardless of which loads first.
  const std::string bootstrap_yaml = R"EOF(
      name: envoy.bootstrap.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
        dynamic_module_config:
          name: bootstrap_http_combined_test
          do_not_close: true
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
    do_not_close: true
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

TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigSnapshotRust) {
  DISABLE_IF_ADMIN_DISABLED;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_admin()->add_allow_paths()->set_prefix("/snapshot");
    auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
    cluster->set_name("snapshot_extra");
    cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    cluster->mutable_connect_timeout()->set_seconds(1);
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_snapshot_test");
  auto denied = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/config_dump", "",
                                                   Http::CodecType::HTTP1, version_);
  EXPECT_EQ("403", denied->headers().getStatusValue());

  auto collect = [this](absl::string_view path) {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", std::string(path), "", Http::CodecType::HTTP1, version_);
    EXPECT_EQ("200", response->headers().getStatusValue());
    envoy::admin::v3::ConfigDump dump;
    TestUtility::loadFromJson(response->body(), dump);
    return dump;
  };
  auto snapshot = collect("/snapshot");
  ASSERT_EQ(3, snapshot.configs_size());
  envoy::admin::v3::ClustersConfigDump clusters;
  ASSERT_TRUE(snapshot.configs(0).UnpackTo(&clusters));
  ASSERT_EQ(2, clusters.static_clusters_size());
  std::vector<std::string> cluster_names;
  for (const auto& entry : clusters.static_clusters()) {
    envoy::config::cluster::v3::Cluster cluster;
    ASSERT_TRUE(entry.cluster().UnpackTo(&cluster));
    cluster_names.push_back(cluster.name());
  }
  EXPECT_THAT(cluster_names, testing::UnorderedElementsAre("cluster_0", "snapshot_extra"));
  envoy::admin::v3::EndpointsConfigDump endpoints;
  ASSERT_TRUE(snapshot.configs(1).UnpackTo(&endpoints));
  ASSERT_EQ(2, endpoints.static_endpoint_configs_size());
  std::vector<std::string> endpoint_cluster_names;
  for (const auto& entry : endpoints.static_endpoint_configs()) {
    envoy::config::endpoint::v3::ClusterLoadAssignment assignment;
    ASSERT_TRUE(entry.endpoint_config().UnpackTo(&assignment));
    endpoint_cluster_names.push_back(assignment.cluster_name());
  }
  EXPECT_THAT(endpoint_cluster_names, testing::UnorderedElementsAre("cluster_0", "snapshot_extra"));
  envoy::admin::v3::ListenersConfigDump listeners;
  ASSERT_TRUE(snapshot.configs(2).UnpackTo(&listeners));
  ASSERT_EQ(1, listeners.dynamic_listeners_size());
  envoy::config::listener::v3::Listener listener;
  ASSERT_TRUE(listeners.dynamic_listeners(0).active_state().listener().UnpackTo(&listener));
  EXPECT_EQ("http", listener.name());
  EXPECT_FALSE(listener.filter_chains().empty());

  snapshot = collect("/snapshot?filtered");
  ASSERT_EQ(3, snapshot.configs_size());
  ASSERT_TRUE(snapshot.configs(0).UnpackTo(&clusters));
  ASSERT_EQ(1, clusters.static_clusters_size());
  envoy::config::cluster::v3::Cluster cluster;
  ASSERT_TRUE(clusters.static_clusters(0).cluster().UnpackTo(&cluster));
  EXPECT_EQ("cluster_0", cluster.name());
  ASSERT_TRUE(snapshot.configs(1).UnpackTo(&endpoints));
  ASSERT_EQ(1, endpoints.static_endpoint_configs_size());
  envoy::config::endpoint::v3::ClusterLoadAssignment assignment;
  ASSERT_TRUE(endpoints.static_endpoint_configs(0).endpoint_config().UnpackTo(&assignment));
  EXPECT_EQ("cluster_0", assignment.cluster_name());
  ASSERT_EQ(1, assignment.endpoints_size());
  const auto& locality = assignment.endpoints(0);
  ASSERT_EQ(1, locality.lb_endpoints_size());
  const auto& address = locality.lb_endpoints(0).endpoint().address().socket_address();
  EXPECT_EQ(fake_upstreams_[0]->localAddress()->ip()->addressAsString(), address.address());
  EXPECT_EQ(fake_upstreams_[0]->localAddress()->ip()->port(), address.port_value());
  ASSERT_TRUE(snapshot.configs(2).UnpackTo(&listeners));
  EXPECT_EQ(0, listeners.dynamic_listeners_size());
}

// Verifies a Rust bootstrap extension can enumerate active resource names by kind via
// active_resource_names() and check that a set of expected names is present.
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesRust) {
  DISABLE_IF_ADMIN_DISABLED;
  // Name the default listener's filter chain so the module observes it by name.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_filter_chains(0)->set_name("chain_0");
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_names_test");

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The static upstream cluster and the named filter chain are observed, and the subset check
  // passes.
  EXPECT_THAT(response->body(), testing::HasSubstr("cluster_0"));
  EXPECT_THAT(response->body(), testing::HasSubstr("chain_0"));
  EXPECT_THAT(response->body(), testing::HasSubstr("present=true"));
}

// Verifies the FilterChain accessor enumerates FCDS filter chains. The listener uses fcds_config,
// so its filter chain (`fc_a`) is delivered as a standalone FilterChain xDS resource and lives in
// the shared FCDS manager, not the listener's inline FilterChainManager. It must still be observed
// (this fails when the accessor only reads inline chains).
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesFcdsRust) {
  DISABLE_IF_ADMIN_DISABLED;
  // A file-based FCDS resource carrying one filter chain named `fc_a` (HCM -> cluster_0).
  const std::string fcds_yaml = R"EOF(
version_info: "1"
resources:
- "@type": type.googleapis.com/envoy.config.listener.v3.FilterChain
  name: fc_a
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      stat_prefix: fc_a
      route_config:
        name: fcds_route
        virtual_hosts:
        - name: fcds_vhost
          domains: ["*"]
          routes:
          - match: {prefix: "/"}
            route: {cluster: cluster_0}
      http_filters:
      - name: envoy.filters.http.router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF";
  const std::string fcds_path =
      TestEnvironment::writeStringToFileForTest("fcds_fc_a.yaml", fcds_yaml);

  config_helper_.addConfigModifier([fcds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    // Move the listener to FCDS: drop the inline chain, point fcds_config at the file, and select
    // `fc_a` for every connection via the matcher's on_no_match.
    listener->mutable_filter_chains()->Clear();
    auto* config_source = listener->mutable_fcds_config()->mutable_config_source();
    config_source->mutable_path_config_source()->set_path(fcds_path);
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    const std::string matcher_yaml = R"EOF(
      on_no_match:
        action:
          name: filter-chain-name
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: fc_a
    )EOF";
    TestUtility::loadFromYaml(matcher_yaml, *listener->mutable_filter_chain_matcher());
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_names_test");

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The FCDS-delivered filter chain is observed under the FilterChain kind.
  EXPECT_THAT(response->body(), testing::HasSubstr("fc_a"));
}

// Verifies the accessors reflect resource removal: a dynamic cluster delivered via file-based CDS
// is observed, then removed by rewriting the CDS file. Once the cluster manager applies the removal
// the module no longer reports it, while the static cluster remains. Guards the "drained resource
// disappears from the getter" path.
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesClusterRemovalRust) {
  DISABLE_IF_ADMIN_DISABLED;
  const std::string cds_with = R"EOF(
version_info: "1"
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster_dyn
  connect_timeout: 0.25s
  type: STATIC
  load_assignment:
    cluster_name: cluster_dyn
    endpoints: []
)EOF";
  const std::string cds_empty = R"EOF(
version_info: "2"
resources: []
)EOF";
  const std::string cds_readd = R"EOF(
version_info: "3"
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster_dyn
  connect_timeout: 0.25s
  type: STATIC
  load_assignment:
    cluster_name: cluster_dyn
    endpoints: []
)EOF";
  const std::string cds_path =
      TestEnvironment::writeStringToFileForTest("config_names_cds.yaml", cds_with);
  config_helper_.addConfigModifier([cds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cds = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
    cds->mutable_path_config_source()->set_path(cds_path);
    cds->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_names_test");

  // The dynamic cluster is observed alongside the static one.
  BufferingStreamDecoderPtr before = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", before->headers().getStatusValue());
  EXPECT_THAT(before->body(), testing::HasSubstr("cluster_dyn"));
  EXPECT_THAT(before->body(), testing::HasSubstr("cluster_0"));

  // Remove the dynamic cluster by atomically replacing the CDS file, then wait for the cluster
  // manager to apply the removal (deterministic, no sleep).
  const std::string cds_empty_path =
      TestEnvironment::writeStringToFileForTest("config_names_cds_empty.yaml", cds_empty);
  TestEnvironment::renameFile(cds_empty_path, cds_path);
  test_server_->waitForCounter("cluster_manager.cluster_removed", testing::Ge(1));

  // The drained cluster is gone from the accessor; the static cluster remains.
  BufferingStreamDecoderPtr after = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", after->headers().getStatusValue());
  EXPECT_THAT(after->body(), testing::Not(testing::HasSubstr("cluster_dyn")));
  EXPECT_THAT(after->body(), testing::HasSubstr("cluster_0"));

  // Re-add the dynamic cluster: the accessor observes it again, so the getter tracks churn both
  // ways (added -> removed -> added), not just the initial state.
  const std::string cds_readd_path =
      TestEnvironment::writeStringToFileForTest("config_names_cds_readd.yaml", cds_readd);
  TestEnvironment::renameFile(cds_readd_path, cds_path);
  test_server_->waitForCounter("cluster_manager.cluster_added", testing::Ge(2));
  BufferingStreamDecoderPtr readded = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", readded->headers().getStatusValue());
  EXPECT_THAT(readded->body(), testing::HasSubstr("cluster_dyn"));
}

namespace {

// Appends a STATIC cluster named `name`; for each entry in `match_names` it adds a transport socket
// match of that name backed by a raw_buffer socket, so the cluster's transportSocketMatcher reports
// exactly those match names.
void addClusterWithTransportSocketMatches(envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                          const std::string& name,
                                          const std::vector<std::string>& match_names) {
  auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
  TestUtility::loadFromYaml(fmt::format(R"EOF(
name: {}
connect_timeout: 0.25s
type: STATIC
load_assignment:
  cluster_name: {}
  endpoints: []
)EOF",
                                        name, name),
                            *cluster);
  for (const std::string& match_name : match_names) {
    TestUtility::loadFromYaml(fmt::format(R"EOF(
name: {}
match:
  stage: "{}"
transport_socket:
  name: envoy.transport_sockets.raw_buffer
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
)EOF",
                                          match_name, match_name),
                              *cluster->add_transport_socket_matches());
  }
}

} // namespace

// The TransportSocketMatch accessor reports the intersection over clusters that have matches:
// `cluster_tsm_a` carries {m1, m2} and `cluster_tsm_b` carries {m1}, so only m1 (present in both)
// is reported.
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesTransportSocketMatchIntersectionRust) {
  DISABLE_IF_ADMIN_DISABLED;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    addClusterWithTransportSocketMatches(bootstrap, "cluster_tsm_a", {"m1", "m2"});
    addClusterWithTransportSocketMatches(bootstrap, "cluster_tsm_b", {"m1"});
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_names_test");

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("transport_socket_matches=[m1]"));
  EXPECT_THAT(response->body(), testing::Not(testing::HasSubstr("m2")));
}

// A cluster with no transport socket matches does not empty the intersection: `cluster_tsm_a`
// carries {m1, m2} and `cluster_tsm_bare` carries none, so both m1 and m2 remain reported.
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesTransportSocketMatchEmptyClusterRust) {
  DISABLE_IF_ADMIN_DISABLED;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    addClusterWithTransportSocketMatches(bootstrap, "cluster_tsm_a", {"m1", "m2"});
    addClusterWithTransportSocketMatches(bootstrap, "cluster_tsm_bare", {});
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_names_test");

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("transport_socket_matches=[m1,m2]"));
}

// A transport socket match drops from the reported set once it is no longer present in every
// matched cluster. A dynamic cluster is delivered with {m1, m2}; rewriting it to carry only {m1}
// shrinks the reported set to {m1}, so m2 is no longer reported.
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesTransportSocketMatchRemovalRust) {
  DISABLE_IF_ADMIN_DISABLED;
  const std::string cds_two_matches = R"EOF(
version_info: "1"
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster_tsm_dyn
  connect_timeout: 0.25s
  type: STATIC
  load_assignment:
    cluster_name: cluster_tsm_dyn
    endpoints: []
  transport_socket_matches:
  - name: m1
    match:
      stage: "m1"
    transport_socket:
      name: envoy.transport_sockets.raw_buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
  - name: m2
    match:
      stage: "m2"
    transport_socket:
      name: envoy.transport_sockets.raw_buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
)EOF";
  const std::string cds_one_match = R"EOF(
version_info: "2"
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster_tsm_dyn
  connect_timeout: 0.25s
  type: STATIC
  load_assignment:
    cluster_name: cluster_tsm_dyn
    endpoints: []
  transport_socket_matches:
  - name: m1
    match:
      stage: "m1"
    transport_socket:
      name: envoy.transport_sockets.raw_buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
)EOF";
  const std::string cds_path =
      TestEnvironment::writeStringToFileForTest("config_names_tsm_cds.yaml", cds_two_matches);
  config_helper_.addConfigModifier([cds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cds = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
    cds->mutable_path_config_source()->set_path(cds_path);
    cds->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_names_test");

  BufferingStreamDecoderPtr before = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", before->headers().getStatusValue());
  EXPECT_THAT(before->body(), testing::HasSubstr("transport_socket_matches=[m1,m2]"));

  // Rewrite the CDS file so the cluster carries only m1, then wait for the update to apply.
  const std::string cds_one_path =
      TestEnvironment::writeStringToFileForTest("config_names_tsm_cds_one.yaml", cds_one_match);
  TestEnvironment::renameFile(cds_one_path, cds_path);
  test_server_->waitForCounter("cluster_manager.cluster_modified", testing::Ge(1));

  BufferingStreamDecoderPtr after = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", after->headers().getStatusValue());
  EXPECT_THAT(after->body(), testing::HasSubstr("transport_socket_matches=[m1]"));
  EXPECT_THAT(after->body(), testing::Not(testing::HasSubstr("m2")));
}

// A filter chain is reported only while it is committed in an active listener. `fc_a` is delivered
// via FCDS and observed; removing it from the FCDS resource set drops it from the reported names.
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesFcdsRemovalRust) {
  DISABLE_IF_ADMIN_DISABLED;
  const std::string fcds_with_fc_a = R"EOF(
version_info: "1"
resources:
- "@type": type.googleapis.com/envoy.config.listener.v3.FilterChain
  name: fc_a
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      stat_prefix: fc_a
      route_config:
        name: fcds_route
        virtual_hosts:
        - name: fcds_vhost
          domains: ["*"]
          routes:
          - match: {prefix: "/"}
            route: {cluster: cluster_0}
      http_filters:
      - name: envoy.filters.http.router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF";
  const std::string fcds_empty = R"EOF(
version_info: "2"
resources: []
)EOF";
  const std::string fcds_path =
      TestEnvironment::writeStringToFileForTest("fcds_removal_fc_a.yaml", fcds_with_fc_a);

  config_helper_.addConfigModifier([fcds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_filter_chains()->Clear();
    auto* config_source = listener->mutable_fcds_config()->mutable_config_source();
    config_source->mutable_path_config_source()->set_path(fcds_path);
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    const std::string matcher_yaml = R"EOF(
      on_no_match:
        action:
          name: filter-chain-name
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: fc_a
    )EOF";
    TestUtility::loadFromYaml(matcher_yaml, *listener->mutable_filter_chain_matcher());
  });
  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_config_names_test");

  BufferingStreamDecoderPtr before = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", before->headers().getStatusValue());
  EXPECT_THAT(before->body(), testing::HasSubstr("fc_a"));

  // Remove `fc_a` from FCDS and wait for the removal update to apply, then confirm it is no longer
  // reported (first delivery bumps update_success to 1; the removal update bumps it to 2).
  const std::string fcds_empty_path =
      TestEnvironment::writeStringToFileForTest("fcds_removal_empty.yaml", fcds_empty);
  TestEnvironment::renameFile(fcds_empty_path, fcds_path);
  test_server_->waitForCounter("filter_chain_manager.fc_a.update_success", testing::Ge(2));

  BufferingStreamDecoderPtr after = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/config_names", "", Http::CodecType::HTTP1, version_);
  EXPECT_EQ("200", after->headers().getStatusValue());
  EXPECT_THAT(after->body(), testing::Not(testing::HasSubstr("fc_a")));
}

// Verifies a Rust bootstrap extension receives secret lifecycle events. The default listener is
// given a file-based SDS TLS certificate, so a dynamic secret provider is created and active by the
// time the module enables secret lifecycle; enabling replays the active secret, and the module
// observes on_secret_add_or_update with the secret name.
TEST_P(DynamicModulesBootstrapIntegrationTest, SecretLifecycleRust) {
  const std::string sds_yaml =
      fmt::format(R"EOF(
---
version_info: "0"
resources:
- "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
  name: "secret_0"
  tls_certificate:
    certificate_chain:
      filename: "{}"
    private_key:
      filename: "{}"
)EOF",
                  TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"),
                  TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
  const std::string sds_path =
      TestEnvironment::writeStringToFileForTest("secret_0.sds.yaml", sds_yaml);

  config_helper_.addConfigModifier([sds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* transport_socket = bootstrap.mutable_static_resources()
                                 ->mutable_listeners(0)
                                 ->mutable_filter_chains(0)
                                 ->mutable_transport_socket();
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* secret_config =
        tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
    secret_config->set_name("secret_0");
    auto* config_source = secret_config->mutable_sds_config();
    config_source->mutable_path_config_source()->set_path(sds_path);
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    transport_socket->set_name("envoy.transport_sockets.tls");
    ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(tls_context));
  });

  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap secret lifecycle test: server initialized"},
                                  {"info", "Secret lifecycle enabled: true"},
                                  {"info", "Secret added or updated: secret_0"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_secret_lifecycle_test"));
  EXPECT_LOG_NOT_CONTAINS("info", "Secret removed: secret_0", { test_server_.reset(); });
}

// Verifies the module observes a secret rotation. After the initial add, rewriting the SDS file
// with a different certificate delivers a provider update, and on_secret_add_or_update fires again
// for secret_0.
TEST_P(DynamicModulesBootstrapIntegrationTest, SecretRotationRust) {
  auto sds_yaml = [](absl::string_view version, absl::string_view cert, absl::string_view key) {
    return fmt::format(R"EOF(
---
version_info: "{}"
resources:
- "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
  name: "secret_0"
  tls_certificate:
    certificate_chain:
      filename: "{}"
    private_key:
      filename: "{}"
)EOF",
                       version, cert, key);
  };
  const std::string cert0 =
      TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem");
  const std::string key0 =
      TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem");
  const std::string cert1 =
      TestEnvironment::runfilesPath("test/config/integration/certs/server2cert.pem");
  const std::string key1 =
      TestEnvironment::runfilesPath("test/config/integration/certs/server2key.pem");
  const std::string sds_path =
      TestEnvironment::writeStringToFileForTest("secret_0.sds.yaml", sds_yaml("0", cert0, key0));

  config_helper_.addConfigModifier([sds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* transport_socket = bootstrap.mutable_static_resources()
                                 ->mutable_listeners(0)
                                 ->mutable_filter_chains(0)
                                 ->mutable_transport_socket();
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* secret_config =
        tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
    secret_config->set_name("secret_0");
    auto* config_source = secret_config->mutable_sds_config();
    config_source->mutable_path_config_source()->set_path(sds_path);
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    transport_socket->set_name("envoy.transport_sockets.tls");
    ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(tls_context));
  });

  initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_secret_lifecycle_test");
  test_server_->waitForCounter("sds.secret_0.update_success", testing::Ge(1));

  // Rotate the certificate by rewriting the SDS file; the provider update fires and the module
  // observes on_secret_add_or_update again for secret_0 (first delivery already logged during
  // init).
  EXPECT_LOG_CONTAINS("info", "Secret added or updated: secret_0", {
    const std::string rotated_path = TestEnvironment::writeStringToFileForTest(
        "secret_0_rotated.sds.yaml", sds_yaml("1", cert1, key1));
    TestEnvironment::renameFile(rotated_path, sds_path);
    test_server_->waitForCounter("sds.secret_0.update_success", testing::Ge(2));
  });
}

// Verifies that a module adds, fails and removes clusters through the cluster writer, that
// requests reach a written cluster, and that its worker event runs on every worker.
TEST_P(DynamicModulesBootstrapIntegrationTest, ClusterWriterRust) {
  DISABLE_IF_ADMIN_DISABLED;
  concurrency_ = 2;
  // Route the requests to a cluster that only exists once the module writes it.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route_config = hcm.mutable_route_config();
        route_config->mutable_validate_clusters()->set_value(false);
        route_config->mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route()->set_cluster(
            "writer.a");
      });
  initializeWithClusterWriter("");

  envoy::config::cluster::v3::Cluster cluster =
      ConfigHelper::buildStaticCluster("writer.a", fake_upstreams_[0]->localAddress()->ip()->port(),
                                       Network::Test::getLoopbackAddressString(version_));
  // The fake upstream speaks HTTP/1.
  cluster.clear_typed_extension_protocol_options();
  std::string serialized_cluster;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_cluster));
  EXPECT_EQ("Success 1", clusterWriterRequest("add=writer.a", serialized_cluster));
  test_server_->waitForCounter("dynamic_modules.cluster_writer.update_active.config_name.writer",
                               testing::Eq(1));
  EXPECT_EQ(1,
            test_server_->gauge("dynamic_modules.cluster_writer.clusters_owned.config_name.writer")
                ->value());
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(1));
  EXPECT_THAT(clusterWriterRequest("outcomes", ""), testing::HasSubstr("1 writer.a Active"));

  // Once the worker event completed, every worker routes to the written cluster.
  EXPECT_EQ("true", clusterWriterRequest("run_on_all_workers=7", ""));
  test_server_->waitForGauge("dynamicmodulescustom.last_done_event_id", testing::Eq(7));
  EXPECT_EQ(2, test_server_->counter("dynamicmodulescustom.worker_events_total")->value());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();

  // A cluster whose configuration has another name fails.
  EXPECT_EQ("Success 2", clusterWriterRequest("add=writer.b", serialized_cluster));
  test_server_->waitForCounter("dynamic_modules.cluster_writer.update_failed.config_name.writer",
                               testing::Eq(1));
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(2));
  EXPECT_THAT(clusterWriterRequest("outcomes", ""),
              testing::HasSubstr("2 writer.b Failed cluster writer.b is named writer.a"));

  EXPECT_EQ("Success", clusterWriterRequest("remove=writer.a", ""));
  test_server_->waitForGauge("dynamic_modules.cluster_writer.clusters_owned.config_name.writer",
                             testing::Eq(0));
  test_server_->waitForCounter("cluster_manager.cluster_removed", testing::Eq(1));
}

// Verifies that CDS keeps a written cluster that its updates leave out, and that the writer gives
// up a cluster that CDS replaces.
TEST_P(DynamicModulesBootstrapIntegrationTest, ClusterWriterWithCdsRust) {
  DISABLE_IF_ADMIN_DISABLED;
  const std::string cds_path = TestEnvironment::writeStringToFileForTest("cluster_writer_cds.yaml",
                                                                         R"EOF(
version_info: "1"
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cds_cluster
  connect_timeout: 0.25s
  type: STATIC
)EOF");
  config_helper_.addConfigModifier([cds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cds = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
    cds->mutable_path_config_source()->set_path(cds_path);
    cds->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
  });
  initializeWithClusterWriter("");

  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("writer.a");
  cluster.mutable_connect_timeout()->set_seconds(1);
  std::string serialized_cluster;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_cluster));
  EXPECT_EQ("Success 1", clusterWriterRequest("add=writer.a", serialized_cluster));
  test_server_->waitForCounter("dynamic_modules.cluster_writer.update_active.config_name.writer",
                               testing::Eq(1));

  // A CDS update without the written cluster removes only the CDS cluster.
  TestEnvironment::renameFile(TestEnvironment::writeStringToFileForTest(
                                  "cluster_writer_cds_empty.yaml", "version_info: \"2\""),
                              cds_path);
  test_server_->waitForCounter("cluster_manager.cluster_removed", testing::Eq(1));
  BufferingStreamDecoderPtr clusters = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/clusters", "", Http::CodecType::HTTP1, version_);
  EXPECT_THAT(clusters->body(), testing::HasSubstr("writer.a::"));
  EXPECT_THAT(clusters->body(), testing::Not(testing::HasSubstr("cds_cluster::")));

  // A CDS update that replaces the written cluster takes it over, so later writes of it fail.
  TestEnvironment::renameFile(TestEnvironment::writeStringToFileForTest("cluster_writer_cds_a.yaml",
                                                                        R"EOF(
version_info: "3"
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: writer.a
  connect_timeout: 2s
  type: STATIC
)EOF"),
                              cds_path);
  test_server_->waitForCounter("dynamic_modules.cluster_writer.ownership_lost.config_name.writer",
                               testing::Eq(1));
  test_server_->waitForGauge("dynamic_modules.cluster_writer.clusters_owned.config_name.writer",
                             testing::Eq(0));
  EXPECT_EQ("Success 2", clusterWriterRequest("add=writer.a", serialized_cluster));
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(2));
  EXPECT_THAT(
      clusterWriterRequest("outcomes", ""),
      testing::HasSubstr("2 writer.a Failed cluster writer.a exists and is not owned by this "
                         "extension"));
  EXPECT_EQ("NotFound", clusterWriterRequest("remove=writer.a", ""));
}

// Verifies that a worker event posted while Envoy starts, before the main dispatcher runs, runs on
// every worker when the module signals that its initialization is complete from a later scheduler
// event.
TEST_P(DynamicModulesBootstrapIntegrationTest, RunOnAllWorkersAtStartupC) {
  concurrency_ = 2;
  initializeWithBootstrapExtension(testDataDir("c"), "bootstrap_optional_hooks", "test",
                                   "run_on_all_workers_at_startup");
  test_server_->waitForCounter("dynamicmodulescustom.worker_events_total", testing::Eq(2));
}

// Verifies that a write from a scheduler event that the module commits in config_new is applied.
TEST_P(DynamicModulesBootstrapIntegrationTest, ClusterWriterStartupRust) {
  DISABLE_IF_ADMIN_DISABLED;
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("writer.startup");
  cluster.mutable_connect_timeout()->set_seconds(1);
  std::string serialized_cluster;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_cluster));
  initializeWithClusterWriter(serialized_cluster);
  test_server_->waitForCounter("dynamic_modules.cluster_writer.update_active.config_name.writer",
                               testing::Eq(1));

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/clusters", "", Http::CodecType::HTTP1, version_);
  EXPECT_THAT(response->body(), testing::HasSubstr("writer.startup::"));
}

// Verifies that a module adds, fails and removes listeners through the listener writer, and that
// a written listener serves once a later worker event completed.
TEST_P(DynamicModulesBootstrapIntegrationTest, ListenerWriterRust) {
  DISABLE_IF_ADMIN_DISABLED;
  concurrency_ = 2;
  initializeWithWriter("");

  std::string serialized_listener;
  ASSERT_TRUE(directResponseListener("writer.a").SerializeToString(&serialized_listener));
  EXPECT_EQ("Success 1", writerRequest("add=writer.a", serialized_listener));
  test_server_->waitForCounter("dynamic_modules.listener_writer.update_active.config_name.writer",
                               testing::Eq(1));
  EXPECT_EQ(
      1, test_server_->gauge("dynamic_modules.listener_writer.listeners_owned.config_name.writer")
             ->value());
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(1));
  EXPECT_THAT(writerRequest("outcomes", ""), testing::HasSubstr("1 writer.a Active"));

  // The written listener serves once the worker event completed.
  runOnAllWorkers(7);
  registerTestServerPorts({"http", "writer"});
  expectDirectResponse("writer");

  // A listener whose configuration has another name fails.
  EXPECT_EQ("Success 2", writerRequest("add=writer.b", serialized_listener));
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(2));
  EXPECT_THAT(writerRequest("outcomes", ""),
              testing::HasSubstr("2 writer.b Failed listener writer.b is named writer.a"));

  // A listener that fails to listen once it warmed is removed.
  envoy::config::listener::v3::Listener bad_listener = directResponseListener("writer.bad");
  auto* socket_option = bad_listener.add_socket_options();
  socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_LISTENING);
  socket_option->set_level(10000);     // Invalid level.
  socket_option->set_int_value(10000); // Invalid value.
  ASSERT_TRUE(bad_listener.SerializeToString(&serialized_listener));
  EXPECT_EQ("Success 3", writerRequest("add=writer.bad", serialized_listener));
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(3));
  EXPECT_THAT(writerRequest("outcomes", ""),
              testing::HasSubstr(
                  "3 writer.bad Failed listener writer.bad failed to listen and was removed"));
  EXPECT_EQ("NotFound", writerRequest("remove=writer.bad", ""));

  EXPECT_EQ("Success", writerRequest("remove=writer.a", ""));
  test_server_->waitForGauge("dynamic_modules.listener_writer.listeners_owned.config_name.writer",
                             testing::Eq(0));
  test_server_->waitForCounter("listener_manager.listener_removed", testing::Eq(2));
}

// Verifies that a listener that the module writes before it signals that its initialization is
// complete serves once Envoy started, and that its write completes as Active.
TEST_P(DynamicModulesBootstrapIntegrationTest, ListenerWriterStartupRust) {
  DISABLE_IF_ADMIN_DISABLED;
  std::string serialized_listener;
  ASSERT_TRUE(directResponseListener("writer.startup").SerializeToString(&serialized_listener));
  initializeWithWriter(serialized_listener);
  EXPECT_EQ(2, test_server_->gauge("listener_manager.total_listeners_active")->value());
  registerTestServerPorts({"http", "writer"});
  expectDirectResponse("writer");

  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(1));
  EXPECT_THAT(writerRequest("outcomes", ""), testing::HasSubstr("1 writer.startup Active"));
}

// Verifies that a State-of-the-World LDS update keeps a written listener that it leaves out, and
// that the writer gives up a listener that LDS replaces. The test framework delivers the http
// listener through LDS.
TEST_P(DynamicModulesBootstrapIntegrationTest, ListenerWriterWithLdsRust) {
  DISABLE_IF_ADMIN_DISABLED;
  initializeWithWriter("");

  std::string serialized_listener;
  ASSERT_TRUE(directResponseListener("writer.a").SerializeToString(&serialized_listener));
  EXPECT_EQ("Success 1", writerRequest("add=writer.a", serialized_listener));
  test_server_->waitForCounter("dynamic_modules.listener_writer.update_active.config_name.writer",
                               testing::Eq(1));

  // An LDS update without the written listener keeps it.
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.setLds("1");
  test_server_->waitForCounter("listener_manager.lds.update_success", testing::Eq(2));
  EXPECT_EQ(2, test_server_->gauge("listener_manager.total_listeners_active")->value());
  EXPECT_EQ(
      1, test_server_->gauge("dynamic_modules.listener_writer.listeners_owned.config_name.writer")
             ->value());

  // An LDS update that replaces the written listener takes it over, so later writes of it fail.
  new_config_helper.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    envoy::config::listener::v3::Listener* listener =
        bootstrap.mutable_static_resources()->add_listeners();
    *listener = directResponseListener("writer.a");
    // The configuration differs from the one that the module writes.
    listener->mutable_per_connection_buffer_limit_bytes()->set_value(32768);
  });
  new_config_helper.setLds("2");
  test_server_->waitForCounter("dynamic_modules.listener_writer.ownership_lost.config_name.writer",
                               testing::Eq(1));
  test_server_->waitForGauge("dynamic_modules.listener_writer.listeners_owned.config_name.writer",
                             testing::Eq(0));
  EXPECT_EQ("Success 2", writerRequest("add=writer.a", serialized_listener));
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(2));
  EXPECT_THAT(
      writerRequest("outcomes", ""),
      testing::HasSubstr("2 writer.a Failed listener writer.a exists and is not owned by this "
                         "extension"));
  EXPECT_EQ("NotFound", writerRequest("remove=writer.a", ""));
}

// Verifies that a listener waits for the filter chain that a module writes, serves its updates, and
// stops serving it once the module removes it.
TEST_P(DynamicModulesBootstrapIntegrationTest, FilterChainConfigWriterRust) {
  DISABLE_IF_ADMIN_DISABLED;
  initializeWithWriter("");

  std::string serialized;
  ASSERT_TRUE(fcdsListener("writer.fcds", "chain.a").SerializeToString(&serialized));
  EXPECT_EQ("Success 1", writerRequest("add=writer.fcds", serialized));
  test_server_->waitForGauge("dynamic_modules.listener_writer.listeners_owned.config_name.writer",
                             testing::Eq(1));
  test_server_->waitForGauge("listener_manager.total_listeners_warming", testing::Eq(1));
  EXPECT_EQ(
      0, test_server_->counter("dynamic_modules.listener_writer.update_active.config_name.writer")
             ->value());
  ASSERT_TRUE(directResponseFilterChain("chain.a").SerializeToString(&serialized));
  EXPECT_EQ("Success 1", writerRequest("add_filter_chain=chain.a", serialized));
  test_server_->waitForCounter(
      "dynamic_modules.filter_chain_config_writer.update_active.config_name.writer",
      testing::Eq(1));
  test_server_->waitForCounter("dynamic_modules.listener_writer.update_active.config_name.writer",
                               testing::Eq(1));
  runOnAllWorkers(1);
  registerTestServerPorts({"http", "writer"});
  expectDirectResponse("writer");

  ASSERT_TRUE(directResponseFilterChain("chain.a", 404).SerializeToString(&serialized));
  EXPECT_EQ("Success 2", writerRequest("add_filter_chain=chain.a", serialized));
  test_server_->waitForCounter(
      "dynamic_modules.filter_chain_config_writer.update_active.config_name.writer",
      testing::Eq(2));
  runOnAllWorkers(2);
  expectDirectResponse("writer", "404");
  // The done hook ran for the listener write and the two filter chain writes.
  test_server_->waitForCounter("dynamicmodulescustom.update_done_total", testing::Eq(3));
  const std::string outcomes = writerRequest("outcomes", "");
  EXPECT_THAT(outcomes, testing::HasSubstr("1 chain.a Active"));
  EXPECT_THAT(outcomes, testing::HasSubstr("2 chain.a Active"));

  EXPECT_EQ("Success", writerRequest("remove_filter_chain=chain.a", ""));
  test_server_->waitForGauge(
      "dynamic_modules.filter_chain_config_writer.filter_chains_owned.config_name.writer",
      testing::Eq(0));
  runOnAllWorkers(3);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("writer"));
  tcp_client->waitForDisconnect();
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
