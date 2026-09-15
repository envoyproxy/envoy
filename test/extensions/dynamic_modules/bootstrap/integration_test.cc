#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
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

// This test verifies that the Rust bootstrap extension can receive listener lifecycle events
// (add/update and removal) via the ListenerUpdateCallbacks mechanism.
TEST_P(DynamicModulesBootstrapIntegrationTest, ListenerLifecycleRust) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "Bootstrap listener lifecycle test: server initialized"},
                                  {"info", "Listener lifecycle enabled: true"}}),
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_listener_lifecycle_test"));
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

const std::string AWS_REQUEST_SIGNING_UPSTREAM_FILTER = R"EOF(
name: envoy.filters.http.aws_request_signing
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
  service_name: execute-api
  region: us-east-1
  signing_algorithm: aws_sigv4
  credential_provider:
    custom_credential_provider_chain: true
    container_credential_provider: {}
)EOF";

class DynamicModulesBootstrapAwsSigningIntegrationTest
    : public DynamicModulesBootstrapIntegrationTest {
public:
  DynamicModulesBootstrapAwsSigningIntegrationTest() {
    // Nothing is listening here, so the fetch fails, anonymous credentials are installed, and the
    // callout goes out unsigned. That doesn't matter. The deadlock is in clearing the pending flag,
    // not in the signature, and both the success and the failure path of the fetch reach it
    // through setCredentialsToAllThreads().
    TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI",
                               "http://127.0.0.1:1/path/to/creds", 1);
  }

  ~DynamicModulesBootstrapAwsSigningIntegrationTest() override {
    // Undo environment changes.
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesBootstrapAwsSigningIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Regression test for a deadlock between the bootstrap init target and AWS credential resolution.
// The module holds its init target open until an HTTP callout through cluster_0 completes, and
// cluster_0 carries an upstream aws_request_signing filter whose credentials chain has no
// synchronous provider, so signing has to wait on an async metadata fetch.
//
// That fetch resolves on the main thread before any worker thread exists. While the resulting
// "credentials are no longer pending" notification was driven from the all-threads-complete
// callback of runOnAllThreads(), it could never fire here: that callback waits for every
// registered worker dispatcher to run the update, worker dispatchers do not run until
// startWorkers(), and startWorkers() waits on the very init manager this module is holding open.
// The signing filter then held the callout until its deadline. Reaching the module's success log at
// all is the assertion; a regression instead fails on the harness giving up waiting for listeners,
// and the module budgets its retries so it cannot spin indefinitely behind that.
TEST_P(DynamicModulesBootstrapAwsSigningIntegrationTest, SignedCalloutGatingInitTarget) {
  // Nothing is servicing the fake upstream while the server is still initializing, so it has to
  // answer the callout on its own.
  autonomous_upstream_ = true;
  config_helper_.prependFilter(AWS_REQUEST_SIGNING_UPSTREAM_FILTER, /*downstream=*/false);
  // cluster_0 carries no protocol options in the base config, and upstream_protocol_options is a
  // required field once any are set. This fills it in without disturbing the filter chain above.
  setUpstreamProtocol(Http::CodecType::HTTP1);

  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap signed callout test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_signed_callout_test"));
}

// Verifies a Rust bootstrap extension can enumerate active resource names by kind via
// active_resource_names() and check that a set of expected names is present.
TEST_P(DynamicModulesBootstrapIntegrationTest, ConfigNamesRust) {
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

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
