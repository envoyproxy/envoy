#include <memory>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/test_filters.pb.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::Ge;
namespace Envoy {
namespace {

constexpr char RuntimeFeatureFilterName[] = "rtds-runtime-feature-test";
constexpr char RuntimeFeaturePath[] = "/runtime-feature";
constexpr char TestRuntimeFeature[] = "envoy.reloadable_features.test_feature_false";

MATCHER_P(RuntimeStatusFilterResponds, expected_status, "") {
  if (arg == nullptr) {
    *result_listener << "response is null";
    return false;
  }
  if (!arg->complete()) {
    *result_listener << "response is incomplete";
    return false;
  }

  const std::string expected_status_string = std::to_string(expected_status);
  const std::string actual_status{arg->headers().getStatusValue()};
  if (actual_status != expected_status_string) {
    *result_listener << "status was " << actual_status;
    return false;
  }

  return true;
}

class RuntimeFeatureTestFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    if (!headers.Path() || headers.getPathValue() != RuntimeFeaturePath) {
      return Http::FilterHeadersStatus::Continue;
    }

    const Http::Code code = Runtime::runtimeFeatureEnabled(TestRuntimeFeature)
                                ? Http::Code::NoContent
                                : static_cast<Http::Code>(418);
    decoder_callbacks_->sendLocalReply(code, "", nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }
};

class RuntimeFeatureTestFilterConfig
    : public Extensions::HttpFilters::Common::UniqueEmptyHttpFilterConfig<
          test::integration::filters::RuntimeFeatureTestFilterConfig> {
public:
  RuntimeFeatureTestFilterConfig() : UniqueEmptyHttpFilterConfig(RuntimeFeatureFilterName) {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamFilter(std::make_shared<RuntimeFeatureTestFilter>());
    };
  }
};

static Registry::RegisterFactory<RuntimeFeatureTestFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_runtime_feature_test_filter_;

// TODO(fredlas) set_node_on_first_message_only was true; the delta+SotW unification
//               work restores it here.
std::string rtdsBootstrapConfig(absl::string_view api_type) {
  return fmt::format(R"EOF(
static_resources:
  clusters:
  - name: dummy_cluster
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {{}}
    load_assignment:
      cluster_name: dummy_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  - name: rtds_cluster
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {{}}
    load_assignment:
      cluster_name: rtds_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
layered_runtime:
  layers:
  - name: some_static_layer
    static_layer:
      foo: whatevs
      bar: yar
  - name: some_rtds_layer
    rtds_layer:
      name: some_rtds_layer
      rtds_config:
        api_config_source:
          api_type: {}
          grpc_services:
            envoy_grpc:
              cluster_name: rtds_cluster
          set_node_on_first_message_only: true
  - name: some_admin_layer
    admin_layer: {{}}
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                     api_type, Platform::null_device_path);
}

void addRuntimeFeatureListener(envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                               Network::Address::IpVersion ip_version) {
  auto* listener = bootstrap.mutable_static_resources()->add_listeners();
  TestUtility::loadFromYaml(fmt::format(R"EOF(
name: http
address:
  socket_address:
    address: "{}"
    port_value: 0
filter_chains:
- filters:
  - name: http
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      stat_prefix: config_test
      delayed_close_timeout:
        nanos: 10000000
      http_filters:
      - name: rtds-runtime-feature-test
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.RuntimeFeatureTestFilterConfig
      - name: envoy.filters.http.router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      codec_type: HTTP2
      route_config:
        name: route_config_0
        virtual_hosts:
        - name: integration
          domains: ["*"]
          routes:
          - match:
              prefix: "/"
            direct_response:
              status: 418
)EOF",
                                        Network::Test::getLoopbackAddressString(ip_version)),
                            *listener);
}

class RtdsIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest, public HttpIntegrationTest {
public:
  RtdsIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecType::HTTP2, ipVersion(),
            rtdsBootstrapConfig(sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
                                        sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
                                    ? "GRPC"
                                    : "DELTA_GRPC")) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                       sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                          ? "true"
                                          : "false");
    use_lds_ = false;
    create_xds_upstream_ = true;
    sotw_or_delta_ = sotwOrDelta();
  }

  void TearDown() override {
    if (xds_connection_ != nullptr) {
      cleanUpXdsConnection();
    }
  }

  void initialize() override {
    // The tests infra expects the xDS server to be the second fake upstream, so
    // we need a dummy data plane cluster.
    setUpstreamCount(1);
    setUpstreamProtocol(Http::CodecType::HTTP2);
    HttpIntegrationTest::initialize();
    // Register admin port.
    registerTestServerPorts({});
    initial_load_success_ = test_server_->counter("runtime.load_success")->value();
    initial_keys_ = test_server_->gauge("runtime.num_keys")->value();
  }

  void acceptXdsConnection() {
    // Initial RTDS connection.
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  std::string getRuntimeKey(const std::string& key) {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/runtime?format=json", "", downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(response->body());
    auto entries = loader->getObject("entries").value();
    if (entries->hasObject(key)) {
      return entries->getObject(key).value()->getString("final_value").value();
    }
    return "";
  }

  BufferingStreamDecoderPtr requestToRuntimeStatusFilter() {
    return IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", RuntimeFeaturePath, "",
                                              downstreamProtocol(), version_);
  }

  uint32_t initial_load_success_{};
  uint32_t initial_keys_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, RtdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

#ifndef WIN32
// TODO): The directory rotation via TestEnvironment::renameFile() fails on Windows. The
// renameFile() implementation does not correctly handle symlinks.

// This test mimics what K8s does when it swaps a ConfigMap. The K8s directory structure looks like:
// 1. ConfigMap mounted to `/config_map/xds`
// 2. Real data directory `/config_map/xds/real_data`
// 3. Real file `/config_map/xds/real_data/xds.yaml`
// 4. Symlink `/config_map/xds/..data` -> `/config_map/xds/real_data`
// 5. Symlink `/config_map/xds/xds.yaml -> `/config_map/xds/..data/xds.yaml`
//
// 2 symlinks are used so that multiple files can be updated in a single atomic swap of ..data to
// a new real target.
TEST_P(RtdsIntegrationTest, FileRtdsReload) {
  // Create an initial setup that looks similar to a K8s ConfigMap deployment. This is a file
  // contained in a directory and referenced via an intermediate symlink on the directory.
  const std::string temp_path{TestEnvironment::temporaryDirectory() + "/rtds_test"};
  TestEnvironment::createPath(temp_path + "/data_1");
  TestEnvironment::writeStringToFileForTest(temp_path + "/data_1/rtds.yaml", R"EOF(
resources:
- "@type": type.googleapis.com/envoy.service.runtime.v3.Runtime
  name: file_rtds
  layer:
 )EOF",
                                            true);
  TestEnvironment::createSymlink(temp_path + "/data_1", temp_path + "/..data");
  TestEnvironment::createSymlink(temp_path + "/..data/rtds.yaml", temp_path + "/rtds.yaml");

  // Create a file based RTDS xDS watch that watches the owning directory for symlink swaps,
  // similar to what would be done when watching a K8s ConfigMap for a swap.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Clear existing layers before adding the file based layer.
    bootstrap.mutable_layered_runtime()->Clear();

    auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
    layer->set_name("file_rtds");
    auto* rtds_layer = layer->mutable_rtds_layer();
    rtds_layer->set_name("file_rtds");
    auto* rtds_config = rtds_layer->mutable_rtds_config();
    rtds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* path_config_source = rtds_config->mutable_path_config_source();
    path_config_source->set_path(temp_path + "/rtds.yaml");
    path_config_source->mutable_watched_directory()->set_path(temp_path);
  });

  initialize();

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(1, test_server_->gauge("runtime.num_layers")->value());

  // Create a new directory and file and then swap at the directory level.
  TestEnvironment::createPath(temp_path + "/data_2");
  TestEnvironment::writeStringToFileForTest(temp_path + "/data_2/rtds.yaml", R"EOF(
resources:
- "@type": type.googleapis.com/envoy.service.runtime.v3.Runtime
  name: file_rtds
  layer:
    foo: bar
 )EOF",
                                            true);
  TestEnvironment::createSymlink(temp_path + "/data_2", temp_path + "/..data.new");
  TestEnvironment::renameFile(temp_path + "/..data.new", temp_path + "/..data");

  test_server_->waitForCounter("runtime.load_success", Ge(initial_load_success_ + 1));
  EXPECT_EQ(1, test_server_->gauge("runtime.num_layers")->value());
  EXPECT_EQ(1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ("bar", getRuntimeKey("foo"));

  TestEnvironment::removePath(temp_path);
}
#endif

TEST_P(RtdsIntegrationTest, RtdsReload) {
  initialize();
  acceptXdsConnection();

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("", getRuntimeKey("baz"));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TestTypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  test_server_->waitForCounter("runtime.load_success", Ge(initial_load_success_ + 1));

  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(initial_load_success_ + 1, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "1", {"some_rtds_layer"},
                                      {}, {}));
  some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      baz: saz
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TestTypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "2");
  test_server_->waitForCounter("runtime.load_success", Ge(initial_load_success_ + 2));

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("saz", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(0, test_server_->counter("runtime.update_failure")->value());
  EXPECT_EQ(initial_load_success_ + 2, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(2, test_server_->counter("runtime.update_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());
}

TEST_P(RtdsIntegrationTest, RtdsOverrideRemovalClearsRuntimeFeature) {
  config_helper_.addConfigModifier(
      [ip_version = ipVersion()](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        addRuntimeFeatureListener(bootstrap, ip_version);
      });
  initialize();
  acceptXdsConnection();

  EXPECT_THAT(requestToRuntimeStatusFilter(), RuntimeStatusFilterResponds(418))
      << "[before RTDS override]";

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      envoy.reloadable_features.test_feature_false: true
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TestTypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  test_server_->waitForCounter("runtime.load_success", Ge(initial_load_success_ + 1));

  EXPECT_THAT(requestToRuntimeStatusFilter(), RuntimeStatusFilterResponds(204))
      << "[after RTDS override]";
  EXPECT_EQ("true", getRuntimeKey(TestRuntimeFeature));
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "1", {"some_rtds_layer"},
                                      {}, {}));
  some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer: {}
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TestTypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "2");
  test_server_->waitForCounter("runtime.load_success", Ge(initial_load_success_ + 2));

  EXPECT_EQ("", getRuntimeKey(TestRuntimeFeature));
  EXPECT_THAT(requestToRuntimeStatusFilter(), RuntimeStatusFilterResponds(418))
      << "[after RTDS override removal]";

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(0, test_server_->counter("runtime.update_failure")->value());
  EXPECT_EQ(initial_load_success_ + 2, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(2, test_server_->counter("runtime.update_success")->value());
  EXPECT_EQ(initial_keys_, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());
}

// Test Rtds update with Resource wrapper.
TEST_P(RtdsIntegrationTest, RtdsUpdate) {
  initialize();
  acceptXdsConnection();

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("", getRuntimeKey("baz"));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");

  // Use the Resource wrapper no matter if it is Sotw or Delta.
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(Config::TestTypeUrl::get().Runtime,
                                                              {some_rtds_layer}, {some_rtds_layer},
                                                              {}, "1", {{"test", Protobuf::Any()}});
  test_server_->waitForCounter("runtime.load_success", Ge(initial_load_success_ + 1));

  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(initial_load_success_ + 1, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());
}

// Verify that RTDS initialization starts only after initialization of all primary clusters has
// completed. Primary cluster initialization completes asynchronously when some of the clusters use
// DNS for endpoint discovery or when health check is configured.
// This test uses health checking of the first cluster to make primary cluster initialization to
// complete asynchronously.
TEST_P(RtdsIntegrationTest, RtdsAfterAsyncPrimaryClusterInitialization) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Enable health checking for the first cluster.
    auto* dummy_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* health_check = dummy_cluster->add_health_checks();
    health_check->mutable_timeout()->set_seconds(30);
    health_check->mutable_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_no_traffic_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_unhealthy_threshold()->set_value(1);
    health_check->mutable_healthy_threshold()->set_value(1);
    health_check->mutable_http_health_check()->set_path("/healthcheck");
    health_check->mutable_http_health_check()->set_codec_client_type(
        envoy::type::v3::CodecClientType::HTTP2);
  });

  initialize();

  // Make sure statically provisioned runtime values were loaded.
  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("", getRuntimeKey("baz"));

  // Wait for the initial health check, which should complete initialization of primary clusters.
  test_server_->waitForCounter("cluster.dummy_cluster.health_check.success", Ge(1));

  // After this xDS connection should be established. Verify that dynamic runtime values are loaded.
  acceptXdsConnection();
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TestTypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  test_server_->waitForCounter("runtime.load_success", Ge(initial_load_success_ + 1));

  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(initial_load_success_ + 1, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());
}

} // namespace
} // namespace Envoy
