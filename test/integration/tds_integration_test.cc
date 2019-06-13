#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

std::string tdsBootstrapConfig(absl::string_view api_type) {
  return fmt::format(R"EOF(
static_resources:
  clusters:
  - name: dummy_cluster
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  - name: tds_cluster
    http2_protocol_options: {{}}
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
layered_runtime:
  layers:
  - name: some_static_layer
    static_layer:
      foo: whatevs
      bar: yar
  - name: some_tds_layer
    tds_layer:
      name: some_tds_layer
      tds_config:
        api_config_source:
          api_type: {}
          grpc_services:
            envoy_grpc:
              cluster_name: tds_cluster
  - name: some_admin_layer
    admin_layer: {{}}
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                     api_type);
}

class TdsIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest, public HttpIntegrationTest {
public:
  TdsIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecClient::Type::HTTP2, ipVersion(),
            tdsBootstrapConfig(sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC")) {
    use_lds_ = false;
    create_xds_upstream_ = true;
    sotw_or_delta_ = sotwOrDelta();
  }

  void TearDown() override {
    cleanUpXdsConnection();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override {
    // The tests infra expects the xDS server to be the second fake upstream, so
    // we need a dummy data plane cluster.
    setUpstreamCount(1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
    // Initial TDS connection.
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
    // Register admin port.
    registerTestServerPorts({});
    initial_load_success_ = test_server_->counter("runtime.load_success")->value();
    initial_keys_ = test_server_->gauge("runtime.num_keys")->value();
  }

  void acceptXdsConnection() {
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  }

  std::string getRuntimeKey(const std::string& key) {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/runtime?format=json", "", downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
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

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, TdsIntegrationTest, DELTA_INTEGRATION_PARAMS);

TEST_P(TdsIntegrationTest, TdsReload) {
  initialize();

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("", getRuntimeKey("baz"));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_tds_layer"},
                                      {"some_tds_layer"}, {}));
  auto some_tds_layer = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: some_tds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");
  sendDiscoveryResponse<envoy::service::discovery::v2::Runtime>(
      Config::TypeUrl::get().Runtime, {some_tds_layer}, {some_tds_layer}, {}, "1");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 1);

  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(initial_load_success_ + 1, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "1", {"some_tds_layer"}, {}, {}));
  some_tds_layer = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: some_tds_layer
    layer:
      baz: saz
  )EOF");
  sendDiscoveryResponse<envoy::service::discovery::v2::Runtime>(
      Config::TypeUrl::get().Runtime, {some_tds_layer}, {some_tds_layer}, {}, "2");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 2);

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("saz", getRuntimeKey("baz"));

  EXPECT_EQ(0, test_server_->counter("runtime.load_error")->value());
  EXPECT_EQ(initial_load_success_ + 2, test_server_->counter("runtime.load_success")->value());
  EXPECT_EQ(initial_keys_ + 1, test_server_->gauge("runtime.num_keys")->value());
  EXPECT_EQ(3, test_server_->gauge("runtime.num_layers")->value());
}

} // namespace
} // namespace Envoy
