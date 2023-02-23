#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class RtdsIntegrationTest : public XdsIntegrationTest {
public:
  void initialize() override {
    XdsIntegrationTest::initialize();

    default_request_headers_.setScheme("http");
    initializeXdsStream();
  }
  void createEnvoy() override {
    builder_.setAggregatedDiscoveryService(Network::Test::getLoopbackAddressUrlString(ipVersion()),
                                           fake_upstreams_[1]->localAddress()->ip()->port());
    // Add the layered runtime config, which includes the RTDS layer.
    builder_.addRtdsLayer("some_rtds_layer", 1);
    XdsIntegrationTest::createEnvoy();
  }

  // using http1 because the h1 cluster has a plaintext socket
  void SetUp() override { setUpstreamProtocol(Http::CodecType::HTTP1); }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDelta, RtdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Envoy Mobile's xDS APIs only support state-of-the-world, not delta.
                     testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::UnifiedSotw)));

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
  EXPECT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  EXPECT_EQ(cc_.on_complete_received_byte_count, 67);
  // Check that the Runtime config is from the static layer.
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.skip_dns_lookup_for_proxied_requests"));

  const std::string load_success_counter = "runtime.load_success";
  uint64_t load_success_value = getCounterValue(load_success_counter);
  // Send a RTDS request and get back the RTDS response.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      envoy.reloadable_features.skip_dns_lookup_for_proxied_requests: True
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  // Wait until the RTDS updates from the DiscoveryResponse have been applied.
  ASSERT_TRUE(waitForCounterGe(load_success_counter, load_success_value + 1));

  // Verify that the Runtime config values are from the RTDS response.
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.skip_dns_lookup_for_proxied_requests"));
}

} // namespace
} // namespace Envoy
