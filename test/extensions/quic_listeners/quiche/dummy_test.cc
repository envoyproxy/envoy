#include "extensions/quic_listeners/quiche/dummy.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {
namespace {

TEST(DummyTest, Dummy) {
  std::string foo = "bar";
  EXPECT_EQ("bar cowbell", moreCowbell(foo));
}

} // namespace
} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
