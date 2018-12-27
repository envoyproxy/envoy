#include "common/quic/dummy.h"

#include "gtest/gtest.h"
#include "quiche/http2/platform/api/http2_string.h"

namespace Envoy {
namespace Quic {

TEST(DummyTest, Dummy) {
  http2::Http2String foo = "bar";
  EXPECT_EQ("bar cowbell", moreCowbell(foo));
}

} // namespace Quic
} // namespace Envoy
