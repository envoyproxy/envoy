#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

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
        resource_api_version: V3
        api_config_source:
          api_type: {}
          transport_api_version: V3
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

class CachedXdsIntegrationTest : public HttpIntegrationTest,
                                 public testing::TestWithParam<Network::Address::IpVersion> {
public:
  CachedXdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam(), rtdsBootstrapConfig("GRPC")) {
    use_lds_ = false;
    create_xds_upstream_ = true;
    // TODO(abeyad): add UnifiedSotw tests too when implementation is ready.
    sotw_or_delta_ = Grpc::SotwOrDelta::Sotw;
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
    auto entries = loader->getObject("entries");
    if (entries->hasObject(key)) {
      return entries->getObject(key)->getString("final_value");
    }
    return "";
  }

  uint32_t initial_load_success_{};
  uint32_t initial_keys_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CachedXdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(CachedXdsIntegrationTest, BasicSuccess) {
  initialize();
  acceptXdsConnection();

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("", getRuntimeKey("baz"));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 1);

  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(initial_load_success_ + 1, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "1", {"some_rtds_layer"}, {}, {}));
  some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      baz: saz
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "2");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 2);

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("saz", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(0, test_server_->counter("runtime.update_failure")->value());
  EXPECT_EQ(initial_load_success_ + 2, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(2, test_server_->counter("runtime.update_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());

  test_server_.reset();

  
}

} // namespace
} // namespace Envoy
