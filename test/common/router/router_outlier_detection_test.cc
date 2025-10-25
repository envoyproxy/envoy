#include "envoy/extensions/filters/http/router/v3/router.pb.h"

#include "source/common/router/router.h"

#include "test/common/router/router_test_base.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {

class RouterOutlierDetectionProcessTest
    : public RouterTestBase,
      public ::testing::WithParamInterface<
          std::tuple<uint64_t, absl::optional<bool>, absl::optional<uint64_t>>> {
public:
  RouterOutlierDetectionProcessTest()
      : RouterTestBase(false, true, false, false, Protobuf::RepeatedPtrField<std::string>{}) {}
};

// Test verifies the interface between router and outlier detection matcher
// defined in cluster's protocol options.
// The router should report to outlier detection success of failure based on the matcher's result,
// not based on response code.
TEST_P(RouterOutlierDetectionProcessTest, OverwriteCodeBasedOnMatcher) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder, &response_decoder, Http::Protocol::Http10);
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  uint64_t code = std::get<0>(GetParam());
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", fmt::format("{}", code)}});
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, processHttpForOutlierDetection(_))
      .WillOnce(Return(std::get<1>(GetParam())));

  bool report_success = (std::get<1>(GetParam()).has_value() && !std::get<1>(GetParam()).value()) ||
                        (!std::get<1>(GetParam()) && (code < 500));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(report_success ? Upstream::Outlier::Result::ExtOriginRequestSuccess
                                       : Upstream::Outlier::Result::ExtOriginRequestFailed,
                        std::get<2>(GetParam())));

  response_decoder->decodeHeaders(std::move(response_headers), true);
}

INSTANTIATE_TEST_SUITE_P(
    RouterOutlierDetectionTestSuite, RouterOutlierDetectionProcessTest,
    ::testing::Values(
        // No matching defined in protocol options. Report the original code to outlier detector.
        std::make_tuple(300, absl::nullopt, absl::optional<uint64_t>(300)),
        // Matching in protocol options took place and did not match the defined matcher.
        // Success (code 200) should be reported to the outlier detection.
        std::make_tuple(300, absl::optional<bool>(false), absl::optional<uint64_t>(200)),
        // Matching in protocol options took place and  matched the defined matcher.
        // Failure (code 500) should be reported to the outlier detection.
        std::make_tuple(300, absl::optional<bool>(true), absl::optional<uint64_t>(500)),
        // Matching in protocol options took place and matched the defined matcher.
        // Since it is 5xx code, the original 5xx code should be reported
        // to the outlier detection.
        std::make_tuple(503, absl::optional<bool>(true), absl::optional<uint64_t>(503)),
        // Matching in protocol options took place and did not matched the defined matcher.
        // Even though it is 5xx code, it should be reported as success
        // to the outlier detection.
        std::make_tuple(503, absl::optional<bool>(false), absl::optional<uint64_t>(200))));

} // namespace Router
} // namespace Envoy
