#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class AdsIntegrationTest : public XdsIntegrationTest {
public:
  void initialize() override {
    XdsIntegrationTest::initialize();

    default_request_headers_.setScheme("http");
    initializeXdsStream();
  }
  void createEnvoy() override {
    // Configure Envoy with API key for authentication
    builder_.setAggregatedDiscoveryService(Network::Test::getLoopbackAddressUrlString(ipVersion()),
                                           fake_upstreams_[1]->localAddress()->ip()->port(), "", 0,
                                           "", "fake_api_key");
    XdsIntegrationTest::createEnvoy();
  }

  // using http1 because the h1 cluster has a plaintext socket
  void SetUp() override { setUpstreamProtocol(Http::CodecType::HTTP1); }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDelta, AdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Envoy Mobile's xDS APIs only support state-of-the-world, not delta.
                     testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::UnifiedSotw)));

TEST_P(AdsIntegrationTest, ApiKeyInHeaders) {
  initialize();

  // Send a request on the data plane.
  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();
  auto xds_stream = xds_stream_.get();
  EXPECT_TRUE(xds_stream->waitForHeadersComplete());
  EXPECT_EQ(xds_stream->headers().get(Envoy::Http::LowerCaseString("x-api-key"))[0]->value(),
            "fake_api_key");
}

} // namespace
} // namespace Envoy
