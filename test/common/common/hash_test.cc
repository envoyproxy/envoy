#include "source/common/common/hash.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Hash, xxHash) {
  EXPECT_EQ(3728699739546630719U, HashUtil::xxHash64("foo"));
  EXPECT_EQ(5234164152756840025U, HashUtil::xxHash64("bar"));
  EXPECT_EQ(8917841378505826757U, HashUtil::xxHash64("foo\nbar"));
  EXPECT_EQ(4400747396090729504U, HashUtil::xxHash64("lyft"));
  EXPECT_EQ(17241709254077376921U, HashUtil::xxHash64(""));
}

TEST(Hash, xxHash64Value) {
  enum Enum { A = 0, B, C };
  enum class ClassEnum { X = 0, Y, Z };
  // Verifying against constants should protect against surprise hash behavior
  // changes, and, when run on a test host with different endianness, should also
  // verify that different endianness doesn't change the result.
  EXPECT_EQ(11149811956558368074UL, HashUtil::xxHash64Value(1234567890123456789UL));
  EXPECT_EQ(5127252389447085590UL, HashUtil::xxHash64Value(-1234567890123456789L));
  EXPECT_EQ(14922725725041217620UL, HashUtil::xxHash64Value(1234567890U));
  EXPECT_EQ(12903803813495632273UL, HashUtil::xxHash64Value(-1234567890));
  EXPECT_EQ(496866755078513886UL, HashUtil::xxHash64Value(1234567890.12345));
  EXPECT_EQ(15789047197365695310UL, HashUtil::xxHash64Value(-1234567890.12345f));
  EXPECT_EQ(9962287286179718960UL, HashUtil::xxHash64Value(true));
  EXPECT_EQ(16804241149081757544UL, HashUtil::xxHash64Value(false));
  EXPECT_EQ(9486749600008296231UL, HashUtil::xxHash64Value(false, /*seed=*/42));
  // 0 as enum, 0 as class enum, and 0 as int32 should all hash to the same value.
  EXPECT_EQ(4246796580750024372, HashUtil::xxHash64Value(A));
  EXPECT_EQ(4246796580750024372, HashUtil::xxHash64Value(ClassEnum::X));
  EXPECT_EQ(4246796580750024372, HashUtil::xxHash64Value(0));
}

TEST(Hash, xxHashWithVector) {
  absl::InlinedVector<absl::string_view, 2> v{"foo", "bar"};
  EXPECT_EQ(17745830980996999794U, HashUtil::xxHash64(absl::MakeSpan(v)));
}

TEST(Hash, djb2CaseInsensitiveHash) {
  EXPECT_EQ(193491849U, HashUtil::djb2CaseInsensitiveHash("foo"));
  EXPECT_EQ(193487034U, HashUtil::djb2CaseInsensitiveHash("bar"));
  EXPECT_EQ(229466047527336U, HashUtil::djb2CaseInsensitiveHash("foo\nbar"));
  EXPECT_EQ(6385457348U, HashUtil::djb2CaseInsensitiveHash("lyft"));
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
