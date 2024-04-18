#include "source/common/grpc/common.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

// For how this value was chosen, see https://github.com/envoyproxy/envoy/issues/17067.
constexpr double ALLOWED_ERROR = 0.10;

const std::string ADMISSION_CONTROL_CONFIG =
    R"EOF(
name: envoy.filters.http.admission_control
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.admission_control.v3.AdmissionControl
  success_criteria:
    http_criteria:
    grpc_criteria:
  sampling_window: 120s
  aggression:
    default_value: 2.0
    runtime_key: "foo.aggression"
  sr_threshold:
    default_value:
      value: 100.0
    runtime_key: "foo.sr_threshold"
  max_rejection_probability:
    default_value:
      value: 100.0
    runtime_key: "foo.mrp"
  enabled:
    default_value: true
    runtime_key: "foo.enabled"
)EOF";

class AdmissionControlIntegrationTest : public Event::TestUsingSimulatedTime,
                                        public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  AdmissionControlIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override {}

  void initialize() override {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
    config_helper_.prependFilter(ADMISSION_CONTROL_CONFIG, downstream_filter_);
    HttpIntegrationTest::initialize();
  }

protected:
  void verifyGrpcSuccess(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("0", response->trailers()->GrpcStatus()->value().getStringView());
  }

  void verifyHttpSuccess(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }

  IntegrationStreamDecoderPtr sendGrpcRequestWithReturnCode(uint64_t code) {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // Set the response headers on the autonomous upstream.
    auto headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
    headers->setStatus(200);
    headers->setContentType("application/grpc");

    auto trailers = std::make_unique<Http::TestResponseTrailerMapImpl>();
    trailers->setGrpcMessage("this is a message");
    trailers->setGrpcStatus(code);

    auto* au = reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get());
    au->setResponseHeaders(std::move(headers));
    au->setResponseTrailers(std::move(trailers));

    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    RELEASE_ASSERT(response->waitForEndStream(), "unexpected timeout");
    codec_client_->close();
    return response;
  }

  IntegrationStreamDecoderPtr sendRequestWithReturnCode(std::string&& code) {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // Set the response headers on the autonomous upstream.
    auto* au = reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get());
    au->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
        Http::TestResponseHeaderMapImpl({{":status", code}})));

    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    RELEASE_ASSERT(response->waitForEndStream(), "unexpected timeout");
    codec_client_->close();
    return response;
  }
  bool downstream_filter_ = true;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdmissionControlIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(AdmissionControlIntegrationTest, HttpTest) {
  autonomous_upstream_ = true;
  initialize();

  // Drop the success rate to a very low value.
  ENVOY_LOG(info, "dropping success rate");
  for (int i = 0; i < 300; ++i) {
    sendRequestWithReturnCode("500");
  }

  // Measure throttling rate from the admission control filter.
  double throttle_count = 0;
  double request_count = 0;
  ENVOY_LOG(info, "validating throttling rate");
  for (int i = 0; i < 300; ++i) {
    auto response = sendRequestWithReturnCode("500");
    auto rc = response->headers().Status()->value().getStringView();
    if (rc == "503") {
      ++throttle_count;
    } else {
      ASSERT_EQ(rc, "500");
    }
    ++request_count;
  }

  // Given the current throttling rate formula with an aggression of 2.0, it should result in a ~98%
  // throttling rate.
  EXPECT_NEAR(throttle_count / request_count, 0.98, ALLOWED_ERROR);

  // We now wait for the history to become stale.
  timeSystem().advanceTimeWait(std::chrono::seconds(120));

  // We expect a 100% success rate after waiting. No throttling should occur.
  for (int i = 0; i < 100; ++i) {
    verifyHttpSuccess(sendRequestWithReturnCode("200"));
  }
}

TEST_P(AdmissionControlIntegrationTest, UpstreamTest) {
  downstream_filter_ = false;
  autonomous_upstream_ = true;
  initialize();

  // Drop the success rate to a very low value.
  ENVOY_LOG(info, "dropping success rate");
  for (int i = 0; i < 300; ++i) {
    sendRequestWithReturnCode("500");
  }

  // Measure throttling rate from the admission control filter.
  double throttle_count = 0;
  double request_count = 0;
  ENVOY_LOG(info, "validating throttling rate");
  for (int i = 0; i < 300; ++i) {
    auto response = sendRequestWithReturnCode("500");
    auto rc = response->headers().Status()->value().getStringView();
    if (rc == "503") {
      ++throttle_count;
    } else {
      ASSERT_EQ(rc, "500");
    }
    ++request_count;
  }

  // Given the current throttling rate formula with an aggression of 2.0, it should result in a ~98%
  // throttling rate.
  EXPECT_NEAR(throttle_count / request_count, 0.98, ALLOWED_ERROR);

  // We now wait for the history to become stale.
  timeSystem().advanceTimeWait(std::chrono::seconds(120));

  // We expect a 100% success rate after waiting. No throttling should occur.
  for (int i = 0; i < 100; ++i) {
    verifyHttpSuccess(sendRequestWithReturnCode("200"));
  }
}

TEST_P(AdmissionControlIntegrationTest, GrpcTest) {
  autonomous_upstream_ = true;
  initialize();

  // Drop the success rate to a very low value.
  for (int i = 0; i < 300; ++i) {
    sendGrpcRequestWithReturnCode(14);
  }

  // Measure throttling rate from the admission control filter.
  double throttle_count = 0;
  double request_count = 0;
  for (int i = 0; i < 300; ++i) {
    auto response = sendGrpcRequestWithReturnCode(10);

    // When the filter is throttling, it returns an HTTP code 503 and the GRPC status is unset.
    // Otherwise, we expect a GRPC status of "Unknown" as set above.
    if (response->headers().Status()->value().getStringView() == "503") {
      ++throttle_count;
    } else {
      auto grpc_status = Grpc::Common::getGrpcStatus(*(response->trailers()));
      ASSERT_EQ(grpc_status, Grpc::Status::WellKnownGrpcStatus::Aborted);
    }
    ++request_count;
  }

  // Given the current throttling rate formula with an aggression of 2.0, it should result in a ~98%
  // throttling rate.
  EXPECT_NEAR(throttle_count / request_count, 0.98, ALLOWED_ERROR);

  // We now wait for the history to become stale.
  timeSystem().advanceTimeWait(std::chrono::seconds(120));

  // We expect a 100% success rate after waiting. No throttling should occur.
  for (int i = 0; i < 100; ++i) {
    verifyGrpcSuccess(sendGrpcRequestWithReturnCode(0));
  }
}

} // namespace
} // namespace Envoy
