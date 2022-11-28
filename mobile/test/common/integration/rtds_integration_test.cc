#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

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
          initial_fetch_timeout:
            seconds: 1
          resource_api_version: V3
          api_config_source:
            api_type: {}
            transport_api_version: V3
            grpc_services:
              envoy_grpc:
                cluster_name: {}
            set_node_on_first_message_only: true
    - name: some_admin_layer
      admin_layer: {{}}
  )EOF",
                                       api_type, XDS_CLUSTER);

  envoy::config::bootstrap::v3::LayeredRuntime config;
  TestUtility::loadFromYaml(yaml, config);
  return config;
}

class RtdsIntegrationTest : public XdsIntegrationTest {
public:
  RtdsIntegrationTest() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add the layered runtime config, which includes the RTDS layer.
      const std::string api_type = sotw_or_delta_ == Grpc::SotwOrDelta::Sotw ||
                                           sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedSotw
                                       ? "GRPC"
                                       : "DELTA_GRPC";
      bootstrap.mutable_layered_runtime()->MergeFrom(layeredRuntimeConfig(api_type));
    });
  }

  void initialize() override {
    XdsIntegrationTest::initialize();
    initializeXdsStream();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, RtdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(RtdsIntegrationTest, RtdsReload) {
  initialize();

  // Send a request on the data plane.
  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
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
  // Wait until the RTDS updates from the DiscoveryResponse have been applied.
  ASSERT_TRUE(waitForCounterGe("runtime.load_success", 1));

  // Verify that the Runtime config values are from the RTDS response.
  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));
}

} // namespace
} // namespace Envoy
