#include "extensions/quic_listeners/quiche/dummy.h"

#include "gtest/gtest.h"
#include "quiche/http2/platform/api/http2_string.h"

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {
namespace {

TEST(DummyTest, Dummy) {
  http2::Http2String foo = "bar";
  EXPECT_EQ("bar cowbell", moreCowbell(foo));
}

} // namespace
} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
