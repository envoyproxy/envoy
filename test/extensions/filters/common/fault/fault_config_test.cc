#include "extensions/filters/common/fault/fault_config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Fault {
namespace {

TEST(FaultConfigTest, FaultDelayHeaderConfig) {
  envoy::config::filter::fault::v2::FaultDelay proto_config;
  proto_config.mutable_header_delay();
  FaultDelayConfig config(proto_config);

  // No header.
  EXPECT_EQ(absl::nullopt, config.duration(nullptr));

  // Header with bad data.
  Http::TestHeaderMapImpl bad_headers{{"x-envoy-throttle-request-latency", "abc"}};
  EXPECT_EQ(absl::nullopt,
            config.duration(bad_headers.get(HeaderNames::get().ThrottleRequestLatency)));
}

TEST(FaultConfigTest, FaultRateLimitHeaderConfig) {
  envoy::config::filter::fault::v2::FaultRateLimit proto_config;
  proto_config.mutable_header_limit();
  FaultRateLimitConfig config(proto_config);

  // No header.
  EXPECT_EQ(absl::nullopt, config.rateKbps(nullptr));

  // Header with bad data.
  Http::TestHeaderMapImpl bad_headers{{"x-envoy-throttle-response-throughput", "abc"}};
  EXPECT_EQ(absl::nullopt,
            config.rateKbps(bad_headers.get(HeaderNames::get().ThrottleResponseThroughput)));

  // Header with zero.
  Http::TestHeaderMapImpl zero_headers{{"x-envoy-throttle-response-throughput", "0"}};
  EXPECT_EQ(absl::nullopt,
            config.rateKbps(zero_headers.get(HeaderNames::get().ThrottleResponseThroughput)));
}

} // namespace
} // namespace Fault
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
