#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

const std::string ADMISSION_CONTROL_CONFIG =
    R"EOF(
name: envoy.filters.http.admission_control
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.admission_control.v2alpha.AdmissionControl
  default_success_criteria:
    http_status:
    grpc_status:
  sampling_window: 10s
  aggression_coefficient:
    default_value: 1.0
    runtime_key: "foo.aggression"
)EOF";

class AdmissionControlIntegrationTest : public Event::TestUsingSimulatedTime,
                                        public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  AdmissionControlIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  void SetUp() override {
    autonomous_upstream_ = true;
    initialize();
  }

  void initialize() override {
    config_helper_.addFilter(ADMISSION_CONTROL_CONFIG);
    HttpIntegrationTest::initialize();
  }

protected:
  void verifySuccess(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }

  void verifyFailure(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  }

  void waitFor(std::chrono::microseconds us) { timeSystem().sleep(us); }

  IntegrationStreamDecoderPtr sendRequestWithReturnCode(std::string&& code) {
    IntegrationCodecClientPtr codec_client;
    codec_client = makeHttpConnection(lookupPort("http"));

    // Set the response headers on the autonomous upstream.
    auto* au = reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get());
    au->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
        Http::TestHeaderMapImpl({{":status", code}})));

    auto response = codec_client->makeHeaderOnlyRequest(default_request_headers_);
    response->waitForEndStream();
    codec_client->close();
    return response;
  }

  void sendGrpcRequest(Grpc::Status::GrpcStatus status) {
    fake_upstreams_.front()->startGrpcStream();
    fake_upstreams_.front()->finishGrpcStream(status);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdmissionControlIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(AdmissionControlIntegrationTest, HttpTest) {
  // Drop the success rate to a very low value.
  ENVOY_LOG(info, "dropping success rate");
  for (int i = 0; i < 1000; ++i) {
    sendRequestWithReturnCode("500");
  }

  // Measure throttling rate from the admission control filter.
  double throttle_count = 0;
  double request_count = 0;
  ENVOY_LOG(info, "validating throttling rate");
  for (int i = 0; i < 1000; ++i) {
    auto response = sendRequestWithReturnCode("500");
    auto rc = response->headers().Status()->value().getStringView();
    if (rc == "503") {
      ++throttle_count;
    } else {
      ASSERT_EQ(rc, "500");
    }
    ++request_count;
  }

  // Given the current throttling rate formula with an aggression of 1, it should result in a >98%
  // throttling rate.
  EXPECT_GE(throttle_count / request_count, 0.98);

  // We now wait for the history to become stale.
  waitFor(std::chrono::seconds(10));

  // We expect a 100% success rate after waiting. No throttling should occur.
  for (int i = 0; i < 100; ++i) {
    verifySuccess(sendRequestWithReturnCode("200"));
  }
}

// TODO (tonya11en): GRPC test.

} // namespace
} // namespace Envoy
