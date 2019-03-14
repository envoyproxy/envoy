#include "common/stats/char_star_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

TEST(Hash, charStarSet) {
  CharStarSet set;
  const char* foo = set.insert("foo");
  EXPECT_EQ(foo, set.find("foo"));
  EXPECT_EQ(foo, set.find(std::string("foo")));
}

} // namespace Stats
} // namespace Envoy
