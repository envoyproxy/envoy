#include "extensions/filters/datagram/quiche/dummy.h"

#include "gtest/gtest.h"
#include "quiche/http2/platform/api/http2_string.h"

namespace Envoy {
namespace Extensions {
namespace DatagramFilters {
namespace Quiche {

TEST(DummyTest, Dummy) {
  http2::Http2String foo = "bar";
  EXPECT_EQ("bar cowbell", moreCowbell(foo));
}

} // namespace Quiche
} // namespace DatagramFilters
} // namespace Extensions
} // namespace Envoy
