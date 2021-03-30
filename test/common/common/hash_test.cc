#include "common/common/hash.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Hash, xxHash) {
  EXPECT_EQ(3728699739546630719U, HashUtil::xxHash64("foo"));
  EXPECT_EQ(5234164152756840025U, HashUtil::xxHash64("bar"));
  EXPECT_EQ(8917841378505826757U, HashUtil::xxHash64("foo\nbar"));
  EXPECT_EQ(4400747396090729504U, HashUtil::xxHash64("lyft"));
  EXPECT_EQ(17241709254077376921U, HashUtil::xxHash64(""));
}

TEST(Hash, djb2CaseInsensitiveHash) {
  EXPECT_EQ(211616621U, HashUtil::djb2CaseInsensitiveHash("foo"));
  EXPECT_EQ(211611524U, HashUtil::djb2CaseInsensitiveHash("bar"));
  EXPECT_EQ(282790909350396U, HashUtil::djb2CaseInsensitiveHash("foo\nbar"));
  EXPECT_EQ(7195212308U, HashUtil::djb2CaseInsensitiveHash("lyft"));
  EXPECT_EQ(5381U, HashUtil::djb2CaseInsensitiveHash(""));
}

TEST(Hash, murmurHash2) {
  EXPECT_EQ(9631199822919835226U, MurmurHash::murmurHash2("foo"));
  EXPECT_EQ(11474628671133349555U, MurmurHash::murmurHash2("bar"));
  EXPECT_EQ(16306510975912980159U, MurmurHash::murmurHash2("foo\nbar"));
  EXPECT_EQ(12847078931730529320U, MurmurHash::murmurHash2("lyft"));
  EXPECT_EQ(6142509188972423790U, MurmurHash::murmurHash2(""));
}

#if __GLIBCXX__ >= 20130411 && __GLIBCXX__ <= 20180726
TEST(Hash, stdhash) {
  EXPECT_EQ(std::hash<std::string>()(std::string("foo")), MurmurHash::murmurHash2("foo"));
  EXPECT_EQ(std::hash<std::string>()(std::string("bar")), MurmurHash::murmurHash2("bar"));
  EXPECT_EQ(std::hash<std::string>()(std::string("foo\nbar")), MurmurHash::murmurHash2("foo\nbar"));
  EXPECT_EQ(std::hash<std::string>()(std::string("lyft")), MurmurHash::murmurHash2("lyft"));
  EXPECT_EQ(std::hash<std::string>()(std::string("")), MurmurHash::murmurHash2(""));
}
#endif

TEST(Hash, sharedStringSet) {
  SharedStringSet set;
  auto foo = std::make_shared<std::string>("foo");
  set.insert(foo);
  auto pos = set.find("foo");
  EXPECT_EQ(pos->get(), foo.get());
}

} // namespace Envoy
