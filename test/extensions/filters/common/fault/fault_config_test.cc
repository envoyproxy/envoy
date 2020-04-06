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

  // No header.
  EXPECT_EQ(absl::nullopt, config.statusCode(nullptr));

  // Header with bad data.
  Http::TestHeaderMapImpl bad_headers{{"x-envoy-fault-abort-request", "abc"}};
  EXPECT_EQ(absl::nullopt, config.statusCode(bad_headers.get(HeaderNames::get().AbortRequest)));

  // Out of range header - value too low.
  Http::TestHeaderMapImpl too_low_headers{{"x-envoy-fault-abort-request", "199"}};
  EXPECT_EQ(absl::nullopt, config.statusCode(too_low_headers.get(HeaderNames::get().AbortRequest)));

  // Out of range header - value too high.
  Http::TestHeaderMapImpl too_high_headers{{"x-envoy-fault-abort-request", "600"}};
  EXPECT_EQ(absl::nullopt,
            config.statusCode(too_high_headers.get(HeaderNames::get().AbortRequest)));

  // Valid header.
  Http::TestHeaderMapImpl good_headers{{"x-envoy-fault-abort-request", "401"}};
  EXPECT_EQ(Http::Code::Unauthorized,
            config.statusCode(good_headers.get(HeaderNames::get().AbortRequest)).value());
}

TEST(FaultConfigTest, FaultDelayHeaderConfig) {
  envoy::extensions::filters::common::fault::v3::FaultDelay proto_config;
  proto_config.mutable_header_delay();
  FaultDelayConfig config(proto_config);

  // No header.
  EXPECT_EQ(absl::nullopt, config.duration(nullptr));

  // Header with bad data.
  Http::TestHeaderMapImpl bad_headers{{"x-envoy-fault-delay-request", "abc"}};
  EXPECT_EQ(absl::nullopt, config.duration(bad_headers.get(HeaderNames::get().DelayRequest)));

  // Valid header.
  Http::TestHeaderMapImpl good_headers{{"x-envoy-fault-delay-request", "123"}};
  EXPECT_EQ(std::chrono::milliseconds(123),
            config.duration(good_headers.get(HeaderNames::get().DelayRequest)).value());
}

TEST(FaultConfigTest, FaultRateLimitHeaderConfig) {
  envoy::extensions::filters::common::fault::v3::FaultRateLimit proto_config;
  proto_config.mutable_header_limit();
  FaultRateLimitConfig config(proto_config);

  // No header.
  EXPECT_EQ(absl::nullopt, config.rateKbps(nullptr));

  // Header with bad data.
  Http::TestHeaderMapImpl bad_headers{{"x-envoy-fault-throughput-response", "abc"}};
  EXPECT_EQ(absl::nullopt, config.rateKbps(bad_headers.get(HeaderNames::get().ThroughputResponse)));

  // Header with zero.
  Http::TestHeaderMapImpl zero_headers{{"x-envoy-fault-throughput-response", "0"}};
  EXPECT_EQ(absl::nullopt,
            config.rateKbps(zero_headers.get(HeaderNames::get().ThroughputResponse)));

  // Valid header.
  Http::TestHeaderMapImpl good_headers{{"x-envoy-fault-throughput-response", "123"}};
  EXPECT_EQ(123UL,
            config.rateKbps(good_headers.get(HeaderNames::get().ThroughputResponse)).value());
}

} // namespace
} // namespace Fault
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
