#include "source/extensions/clusters/redis/twem_hash.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

TEST(TwemHash, tw_hash) {
  EXPECT_EQ(1559806540, TwemHash::hash("foo", 1));
  EXPECT_EQ(1332080621, TwemHash::hash("foo", 2));
  EXPECT_EQ(3826480458, TwemHash::hash("bar", 1));
  EXPECT_EQ(1391875675, TwemHash::hash("bar", 2));
  EXPECT_EQ(4079554067, TwemHash::hash("bilibili", 0));
  EXPECT_EQ(3649838548, TwemHash::hash("", 0));
}

TEST(TwemHash, fnv1a64) {
  EXPECT_EQ(4275688823, TwemHash::fnv1a64("foo"));
  EXPECT_EQ(322520602, TwemHash::fnv1a64("bar"));
  EXPECT_EQ(4156757469, TwemHash::fnv1a64("bilibili"));
  EXPECT_EQ(2216829733, TwemHash::fnv1a64(""));
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
