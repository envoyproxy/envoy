#include "envoy/extensions/filters/common/fault/v3/fault.pb.h"

#include "extensions/filters/common/fault/fault_config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Fault {
namespace {

TEST(FaultConfigTest, FaultAbortHeaderConfig) {
  envoy::extensions::filters::http::fault::v3::FaultAbort proto_config;
  proto_config.mutable_header_abort();
  FaultAbortConfig config(proto_config);

  // Header with bad data.
  Http::TestRequestHeaderMapImpl bad_headers{{"x-envoy-fault-abort-request", "abc"}};
  EXPECT_EQ(absl::nullopt, config.httpStatusCode(&bad_headers));

  // Out of range header - value too low.
  Http::TestRequestHeaderMapImpl too_low_headers{{"x-envoy-fault-abort-request", "199"}};
  EXPECT_EQ(absl::nullopt, config.httpStatusCode(&too_low_headers));

  // Out of range header - value too high.
  Http::TestRequestHeaderMapImpl too_high_headers{{"x-envoy-fault-abort-request", "600"}};
  EXPECT_EQ(absl::nullopt, config.httpStatusCode(&too_high_headers));

  // Valid header.
  Http::TestRequestHeaderMapImpl good_headers{{"x-envoy-fault-abort-request", "401"}};
  EXPECT_EQ(Http::Code::Unauthorized, config.httpStatusCode(&good_headers));
}

TEST(FaultConfigTest, FaultAbortGrpcHeaderConfig) {
  envoy::extensions::filters::http::fault::v3::FaultAbort proto_config;
  proto_config.mutable_header_abort();
  FaultAbortConfig config(proto_config);

  // Header with bad data.
  Http::TestRequestHeaderMapImpl bad_headers{{"x-envoy-fault-abort-grpc-request", "abc"}};
  EXPECT_EQ(absl::nullopt, config.grpcStatusCode(&bad_headers));

  // Out of range header - value too low.
  Http::TestRequestHeaderMapImpl too_low_headers{{"x-envoy-fault-abort-grpc-request", "-1"}};
  EXPECT_EQ(absl::nullopt, config.grpcStatusCode(&too_low_headers));

  // Valid header - with well-defined gRPC status code in [0,16] range.
  Http::TestRequestHeaderMapImpl good_headers{{"x-envoy-fault-abort-grpc-request", "5"}};
  EXPECT_EQ(Grpc::Status::NotFound, config.grpcStatusCode(&good_headers));

  // Valid header - with not well-defined gRPC status code (> 16).
  Http::TestRequestHeaderMapImpl too_high_headers{{"x-envoy-fault-abort-grpc-request", "100"}};
  EXPECT_EQ(100, config.grpcStatusCode(&too_high_headers));
}

TEST(FaultConfigTest, FaultAbortPercentageHeaderConfig) {
  envoy::extensions::filters::http::fault::v3::FaultAbort proto_config;
  proto_config.mutable_header_abort();
  proto_config.mutable_percentage()->set_numerator(80);
  proto_config.mutable_percentage()->set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  FaultAbortConfig config(proto_config);

  // Header with bad data - fallback to proto config.
  Http::TestRequestHeaderMapImpl bad_headers{{"x-envoy-fault-abort-request-percentage", "abc"}};
  const auto bad_headers_percentage = config.percentage(&bad_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), bad_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), bad_headers_percentage.denominator());

  // Out of range header, value too low - fallback to proto config.
  Http::TestRequestHeaderMapImpl too_low_headers{{"x-envoy-fault-abort-request-percentage", "-1"}};
  const auto too_low_headers_percentage = config.percentage(&too_low_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), too_low_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), too_low_headers_percentage.denominator());

  // Valid header with value greater than the value of the numerator of default percentage - use
  // proto config.
  Http::TestRequestHeaderMapImpl good_headers{{"x-envoy-fault-abort-request-percentage", "90"}};
  const auto good_headers_percentage = config.percentage(&good_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), good_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), good_headers_percentage.denominator());

  // Valid header with value lesser than the value of the numerator of default percentage.
  Http::TestRequestHeaderMapImpl greater_numerator_headers{
      {"x-envoy-fault-abort-request-percentage", "60"}};
  const auto greater_numerator_headers_percentage = config.percentage(&greater_numerator_headers);
  EXPECT_EQ(60, greater_numerator_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(),
            greater_numerator_headers_percentage.denominator());
}

TEST(FaultConfigTest, FaultDelayHeaderConfig) {
  envoy::extensions::filters::common::fault::v3::FaultDelay proto_config;
  proto_config.mutable_header_delay();
  FaultDelayConfig config(proto_config);

  // Header with bad data.
  Http::TestRequestHeaderMapImpl bad_headers{{"x-envoy-fault-delay-request", "abc"}};
  EXPECT_EQ(absl::nullopt, config.duration(&bad_headers));

  // Valid header.
  Http::TestRequestHeaderMapImpl good_headers{{"x-envoy-fault-delay-request", "123"}};
  EXPECT_EQ(std::chrono::milliseconds(123), config.duration(&good_headers).value());
}

TEST(FaultConfigTest, FaultDelayPercentageHeaderConfig) {
  envoy::extensions::filters::common::fault::v3::FaultDelay proto_config;
  proto_config.mutable_header_delay();
  proto_config.mutable_percentage()->set_numerator(80);
  proto_config.mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  FaultDelayConfig config(proto_config);

  // Header with bad data - fallback to proto config.
  Http::TestRequestHeaderMapImpl bad_headers{{"x-envoy-fault-delay-request-percentage", "abc"}};
  const auto bad_headers_percentage = config.percentage(&bad_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), bad_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), bad_headers_percentage.denominator());

  // Out of range header, value too low - fallback to proto config.
  Http::TestRequestHeaderMapImpl too_low_headers{{"x-envoy-fault-delay-request-percentage", "-1"}};
  const auto too_low_headers_percentage = config.percentage(&too_low_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), too_low_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), too_low_headers_percentage.denominator());

  // Valid header with value greater than the value of the numerator of default percentage - use
  // proto config.
  Http::TestRequestHeaderMapImpl good_headers{{"x-envoy-fault-delay-request-percentage", "90"}};
  const auto good_headers_percentage = config.percentage(&good_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), good_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), good_headers_percentage.denominator());

  // Valid header with value lesser than the value of the numerator of default percentage.
  Http::TestRequestHeaderMapImpl greater_numerator_headers{
      {"x-envoy-fault-delay-request-percentage", "60"}};
  const auto greater_numerator_headers_percentage = config.percentage(&greater_numerator_headers);
  EXPECT_EQ(60, greater_numerator_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(),
            greater_numerator_headers_percentage.denominator());
}

TEST(FaultConfigTest, FaultRateLimitHeaderConfig) {
  envoy::extensions::filters::common::fault::v3::FaultRateLimit proto_config;
  proto_config.mutable_header_limit();
  FaultRateLimitConfig config(proto_config);

  // Header with bad data.
  Http::TestRequestHeaderMapImpl bad_headers{{"x-envoy-fault-throughput-response", "abc"}};
  EXPECT_EQ(absl::nullopt, config.rateKbps(&bad_headers));

  // Header with zero.
  Http::TestRequestHeaderMapImpl zero_headers{{"x-envoy-fault-throughput-response", "0"}};
  EXPECT_EQ(absl::nullopt, config.rateKbps(&zero_headers));

  // Valid header.
  Http::TestRequestHeaderMapImpl good_headers{{"x-envoy-fault-throughput-response", "123"}};
  EXPECT_EQ(123UL, config.rateKbps(&good_headers).value());
}

TEST(FaultConfigTest, FaultRateLimitPercentageHeaderConfig) {
  envoy::extensions::filters::common::fault::v3::FaultRateLimit proto_config;
  proto_config.mutable_header_limit();
  proto_config.mutable_percentage()->set_numerator(80);
  proto_config.mutable_percentage()->set_denominator(envoy::type::v3::FractionalPercent::MILLION);
  FaultRateLimitConfig config(proto_config);

  // Header with bad data - fallback to proto config.
  Http::TestRequestHeaderMapImpl bad_headers{
      {"x-envoy-fault-throughput-response-percentage", "abc"}};
  const auto bad_headers_percentage = config.percentage(&bad_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), bad_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), bad_headers_percentage.denominator());

  // Out of range header, value too low - fallback to proto config.
  Http::TestRequestHeaderMapImpl too_low_headers{
      {"x-envoy-fault-throughput-response-percentage", "-1"}};
  const auto too_low_headers_percentage = config.percentage(&too_low_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), too_low_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), too_low_headers_percentage.denominator());

  // Valid header with value greater than the value of the numerator of default percentage - use
  // proto config.
  Http::TestRequestHeaderMapImpl good_headers{
      {"x-envoy-fault-throughput-response-percentage", "90"}};
  const auto good_headers_percentage = config.percentage(&good_headers);
  EXPECT_EQ(proto_config.percentage().numerator(), good_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(), good_headers_percentage.denominator());

  // Valid header with value lesser than the value of the numerator of default percentage.
  Http::TestRequestHeaderMapImpl greater_numerator_headers{
      {"x-envoy-fault-throughput-response-percentage", "60"}};
  const auto greater_numerator_headers_percentage = config.percentage(&greater_numerator_headers);
  EXPECT_EQ(60, greater_numerator_headers_percentage.numerator());
  EXPECT_EQ(proto_config.percentage().denominator(),
            greater_numerator_headers_percentage.denominator());
}

} // namespace
} // namespace Fault
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
