#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "source/common/router/rds_impl.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
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
    Router::forceRegisterRdsFactoryImpl();

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

  void runReloadTest() {
    // Send a request on the data plane.
    stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                         true);
    terminal_callback_.waitReady();
    EXPECT_EQ(cc_.on_headers_calls_, 1);
    EXPECT_EQ(cc_.status_, "200");
    EXPECT_EQ(cc_.on_data_calls_, 2);
    EXPECT_EQ(cc_.on_complete_calls_, 1);
    EXPECT_EQ(cc_.on_cancel_calls_, 0);
    EXPECT_EQ(cc_.on_error_calls_, 0);
    EXPECT_EQ(cc_.on_header_consumed_bytes_from_response_, 27);
    EXPECT_EQ(cc_.on_complete_received_byte_count_, 67);
    // Check that the Runtime config is from the static layer.
    EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));

    const std::string load_success_counter = "runtime.load_success";
    uint64_t load_success_value = getCounterValue(load_success_counter);
    // Send a RTDS request and get back the RTDS response.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_resource"},
                                        {"some_rtds_resource"}, {}, true));

    envoy::service::runtime::v3::Runtime some_rtds_resource;
    some_rtds_resource.set_name("some_rtds_resource");
    auto* static_layer = some_rtds_resource.mutable_layer();
    (*static_layer->mutable_fields())["envoy.reloadable_features.test_feature_false"]
        .set_bool_value(true);

    sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
        Config::TypeUrl::get().Runtime, {some_rtds_resource}, {some_rtds_resource}, {}, "1");
    // Wait until the RTDS updates from the DiscoveryResponse have been applied.
    ASSERT_TRUE(waitForCounterGe(load_success_counter, load_success_value + 1));

    // Verify that the Runtime config values are from the RTDS response.
    EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));

    load_success_value = getCounterValue(load_success_counter);
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_resource"},
                                        {"some_rtds_resource"}, {}));
    (*static_layer->mutable_fields())["envoy.reloadable_features.test_feature_false"]
        .set_bool_value(false);

    // Send another response with Resource wrapper.
    sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
        Config::TypeUrl::get().Runtime, {some_rtds_resource}, {some_rtds_resource}, {}, "2",
        {{"test", ProtobufWkt::Any()}});
    // Wait until the RTDS updates from the DiscoveryResponse have been applied.
    ASSERT_TRUE(waitForCounterGe(load_success_counter, load_success_value + 1));

    // Verify that the Runtime config values are from the RTDS response.
    EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDelta, RtdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Envoy Mobile's xDS APIs only support state-of-the-world, not delta.
                     testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::UnifiedSotw)));

TEST_P(RtdsIntegrationTest, RtdsReloadWithDfpMixedScheme) {
  TestScopedStaticReloadableFeaturesRuntime scoped_runtime({{"dfp_mixed_scheme", true}});
  runReloadTest();
}

TEST_P(RtdsIntegrationTest, RtdsReloadWithoutDfpMixedScheme) {
  TestScopedStaticReloadableFeaturesRuntime scoped_runtime({{"dfp_mixed_scheme", false}});
  runReloadTest();
}

} // namespace
} // namespace Envoy
