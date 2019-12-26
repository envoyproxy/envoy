#include "source/extensions/clusters/redis/crc16.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {
TEST(Hash, crc16) {
  EXPECT_EQ(44950, Crc16::crc16("foo"));
  EXPECT_EQ(37829, Crc16::crc16("bar"));
  EXPECT_EQ(3951, Crc16::crc16("foo\nbar"));
  EXPECT_EQ(53222, Crc16::crc16("lyft"));
  EXPECT_EQ(0, Crc16::crc16(""));
}
} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
