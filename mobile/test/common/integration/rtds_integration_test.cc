#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class RtdsIntegrationTest : public XdsIntegrationTest {
public:
  void initialize() override {
    // using http1 because the h1 cluster has a plaintext socket
    setUpstreamProtocol(Http::CodecType::HTTP1);

    XdsIntegrationTest::initialize();

    default_request_headers_.setScheme("http");
    initializeXdsStream();
  }

  void createEnvoy() override {
    Platform::XdsBuilder xds_builder(
        /*xds_server_address=*/Network::Test::getLoopbackAddressUrlString(ipVersion()),
        /*xds_server_port=*/fake_upstreams_[1]->localAddress()->ip()->port());
    // Add the layered runtime config, which includes the RTDS layer.
    xds_builder.addRuntimeDiscoveryService("some_rtds_resource", /*timeout_in_seconds=*/1)
        .setSslRootCerts(getUpstreamCert());
    builder_.setXds(std::move(xds_builder));
    XdsIntegrationTest::createEnvoy();
  }

  void SetUp() override { initialize(); }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDelta, RtdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Envoy Mobile's xDS APIs only support state-of-the-world, not delta.
                     testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::UnifiedSotw)));

TEST_P(RtdsIntegrationTest, RtdsReload) {
  // Send a request on the data plane.
  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();

  EXPECT_EQ(cc_.on_headers_calls, 1);
  EXPECT_EQ(cc_.status, "200");
  EXPECT_EQ(cc_.on_data_calls, 2);
  EXPECT_EQ(cc_.on_complete_calls, 1);
  EXPECT_EQ(cc_.on_cancel_calls, 0);
  EXPECT_EQ(cc_.on_error_calls, 0);
  EXPECT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  EXPECT_EQ(cc_.on_complete_received_byte_count, 67);
  // Check that the Runtime config is from the static layer.
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));

  const std::string load_success_counter = "runtime.load_success";
  uint64_t load_success_value = getCounterValue(load_success_counter);
  // Send a RTDS request and get back the RTDS response.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_resource"},
                                      {"some_rtds_resource"}, {}, true));
  auto some_rtds_resource = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_resource
    layer:
      envoy.reloadable_features.test_feature_false: True
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_resource}, {some_rtds_resource}, {}, "1");
  // Wait until the RTDS updates from the DiscoveryResponse have been applied.
  ASSERT_TRUE(waitForCounterGe(load_success_counter, load_success_value + 1));

  // Verify that the Runtime config values are from the RTDS response.
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));

  load_success_value = getCounterValue(load_success_counter);
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_resource"},
                                      {"some_rtds_resource"}, {}));
  some_rtds_resource = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_resource
    layer:
      envoy.reloadable_features.test_feature_false: False
  )EOF");
  // Send another response with Resource wrapper.
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_resource}, {some_rtds_resource}, {}, "2",
      {{"test", ProtobufWkt::Any()}});
  // Wait until the RTDS updates from the DiscoveryResponse have been applied.
  ASSERT_TRUE(waitForCounterGe(load_success_counter, load_success_value + 1));

  // Verify that the Runtime config values are from the RTDS response.
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
}

} // namespace
} // namespace Envoy
