#include <functional>
#include <string>

#include "test/test_common/logging.h"

#include "gtest/gtest.h"
#include "quiche/spdy/platform/api/spdy_containers.h"
#include "quiche/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "quiche_platform_impl/quiche_flags_impl.h"

// Basic tests to validate functioning of the QUICHE spdy platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {
namespace {

TEST(SpdyPlatformTest, SpdyEstimateMemoryUsage) {
  std::string s = "foo";
  // Stubbed out to always return 0.
  EXPECT_EQ(0, spdy::SpdyEstimateMemoryUsage(s));
}

} // namespace
} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
