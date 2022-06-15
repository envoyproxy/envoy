#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/common/integration/base_client_integration_test.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/http/client.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace {

envoy::config::cluster::v3::Cluster
createSingleEndpointClusterConfig(const std::string& cluster_name,
                                  const std::string& loopbackAddr) {
  envoy::config::cluster::v3::Cluster config;
  config.set_name(cluster_name);

  // Set the endpoint.
  auto* load_assignment = config.mutable_load_assignment();
  load_assignment->set_cluster_name(cluster_name);
  auto* endpoint = load_assignment->add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  endpoint->mutable_address()->mutable_socket_address()->set_address(loopbackAddr);
  endpoint->mutable_address()->mutable_socket_address()->set_port_value(0);

  // Set the protocol options.
  envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
  options.mutable_explicit_http_config()->mutable_http2_protocol_options();
  (*config.mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(options);
  return config;
}

envoy::config::bootstrap::v3::LayeredRuntime layeredRuntimeConfig(const std::string& api_type) {
  const std::string yaml = fmt::format(R"EOF(
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
  )EOF",
                                       api_type);

  envoy::config::bootstrap::v3::LayeredRuntime config;
  TestUtility::loadFromYaml(yaml, config);
  return config;
}

envoy::config::bootstrap::v3::Admin adminConfig(const std::string& loopbackAddr) {
  const std::string yaml = fmt::format(R"EOF(
    access_log:
    - name: envoy.access_loggers.file
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: "{}"
    address:
      socket_address:
        address: {}
        port_value: 0
  )EOF",
                                       ::Platform::null_device_path, loopbackAddr);

  envoy::config::bootstrap::v3::Admin config;
  TestUtility::loadFromYaml(yaml, config);
  return config;
}

class RtdsIntegrationTest : public BaseClientIntegrationTest,
                            public Grpc::DeltaSotwIntegrationParamTest {
public:
  RtdsIntegrationTest() : BaseClientIntegrationTest(ipVersion()) {
    override_builder_config_ = true; // The builder does not yet have RTDS support.
    expect_dns_ = false;             // TODO(alyssawilk) debug.
    create_xds_upstream_ = true;
    sotw_or_delta_ = sotwOrDelta();
    scheme_ = "https";

    if (sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedSotw ||
        sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedDelta) {
      config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux", "true");
    }

    // Set up the cluster config.
    //
    // For now, we clear the default cluster configs and add just two clusters:
    //   - a cluster named "base_h2" because that's what the api_listener is configured to talk to
    //   - an RTDS cluster, for sending and receiving RTDS config
    //
    // The reason we must clear the default cluster configs is because ConfigHelper::setPorts
    // requires that the number of fake upstream ports equal the number of clusters in the config
    // that have dynamic port configuration (i.e. port is 0). In other words, either all fake
    // upstreams must be configured with a dynamic port or none of them (can't mix and match).
    //
    // TODO(abeyad): fix the ConfigHelper::setPorts logic to enable a subset of clusters to have
    // dynamic port configuration.

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      const std::string loopback = loopbackAddr();
      bootstrap.mutable_static_resources()->clear_clusters();
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          createSingleEndpointClusterConfig("base_h2", loopback));
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          createSingleEndpointClusterConfig("rtds_cluster", loopback));
    });

    // xDS upstream is created separately in the test infra, and there's only one non-xDS cluster.
    setUpstreamCount(1);

    // Add the Admin config.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_admin()->MergeFrom(adminConfig(loopbackAddr()));
    });
    admin_filename_ = TestEnvironment::temporaryPath("admin_address.txt");
    setAdminAddressPathForTests(admin_filename_);
  }

  void SetUp() override {
    // TODO(abeyad): Add paramaterized tests for HTTP1, HTTP2, and HTTP3.
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void TearDown() override {
    cleanup();
    BaseClientIntegrationTest::TearDown();
  }

  void createEnvoy() override {
    BaseClientIntegrationTest::createEnvoy();
    std::string admin_str = TestEnvironment::readFileToStringForTest(admin_filename_);
    auto addr = Network::Utility::parseInternetAddressAndPort(admin_str);
    registerPort("admin", addr->ip()->port());
  }

  void initialize() override {
    BaseClientIntegrationTest::initialize();
    // Register admin port.
    // registerTestServerPorts({});

    acceptXdsConnection();
  }

  void addRuntimeRtdsConfig() {
    // Add the layered runtime config, which includes the RTDS layer.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      const std::string api_type = sotw_or_delta_ == Grpc::SotwOrDelta::Sotw ||
                                           sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedSotw
                                       ? "GRPC"
                                       : "DELTA_GRPC";

      bootstrap.mutable_layered_runtime()->MergeFrom(layeredRuntimeConfig(api_type));
    });
  }

  void acceptXdsConnection() {
    // Initial RTDS connection.
    createXdsConnection();
    AssertionResult result =
        xds_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  Grpc::ClientType clientType() const override { return std::get<1>(GetParam()); }
  Grpc::SotwOrDelta sotwOrDelta() const { return std::get<2>(GetParam()); }

  std::string loopbackAddr() const {
    if (ipVersion() == Network::Address::IpVersion::v6) {
      return "::1";
    }
    return "127.0.0.1";
  }

protected:
  std::string getRuntimeKey(const std::string& key) {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/runtime?format=json", "", Http::CodecType::HTTP2, version_);
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
  std::string admin_filename_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, RtdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(RtdsIntegrationTest, RtdsReload) {
  addRuntimeRtdsConfig();
  initialize();

  // Send a request on the data plane.
  stream_->sendHeaders(default_request_headers_, true);
  terminal_callback_.waitReady();

  EXPECT_EQ(cc_.on_headers_calls, 1);
  EXPECT_EQ(cc_.status, "200");
  EXPECT_EQ(cc_.on_data_calls, 2);
  EXPECT_EQ(cc_.on_complete_calls, 1);
  EXPECT_EQ(cc_.on_cancel_calls, 0);
  EXPECT_EQ(cc_.on_error_calls, 0);
  EXPECT_EQ(cc_.on_header_consumed_bytes_from_response, 13);
  EXPECT_EQ(cc_.on_complete_received_byte_count, 41);

  // Check that the Runtime config is from the static layer.
  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("", getRuntimeKey("baz"));

  // Send a RTDS request and get back the RTDS response.
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

  // Verify that the Runtime config values are from the RTDS response.
  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));
}

} // namespace
} // namespace Envoy
