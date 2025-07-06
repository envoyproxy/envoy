#include "source/common/common/radix/trie_hybrid.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;

namespace Envoy {
namespace Common {

TEST(TrieHybrid, AddItems) {
  TrieHybrid<std::string, const char*> trie;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  auto [trie1, _, didUpdate1] = trie.insert("foo", cstr_a);
  EXPECT_TRUE(didUpdate1);
  auto [trie2, _, didUpdate2] = trie1.insert("bar", cstr_b);
  EXPECT_TRUE(didUpdate2);
  EXPECT_EQ(cstr_a, trie2.Get("foo").value());
  EXPECT_EQ(cstr_b, trie2.Get("bar").value());

  // overwrite_existing = false (not supported in TrieHybrid, so we test overwrite)
  auto [trie3, _, didUpdate3] = trie2.insert("foo", cstr_c);
  EXPECT_TRUE(didUpdate3);
  EXPECT_EQ(cstr_c, trie3.Get("foo").value());
}

TEST(TrieHybrid, LongestPrefix) {
  TrieHybrid<std::string, const char*> trie;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";
  const char* cstr_d = "d";
  const char* cstr_e = "e";
  const char* cstr_f = "f";

  auto [trie1, _, didUpdate1] = trie.insert("foo", cstr_a);
  EXPECT_TRUE(didUpdate1);
  auto [trie2, _, didUpdate2] = trie1.insert("bar", cstr_b);
  EXPECT_TRUE(didUpdate2);
  auto [trie3, _, didUpdate3] = trie2.insert("baro", cstr_c);
  EXPECT_TRUE(didUpdate3);
  auto [trie4, _, didUpdate4] = trie3.insert("foo/bar", cstr_d);
  EXPECT_TRUE(didUpdate4);
  auto [trie5, _, didUpdate5] = trie4.insert("barn", cstr_e);
  EXPECT_TRUE(didUpdate5);
  auto [trie6, _, didUpdate6] = trie5.insert("barp", cstr_f);
  EXPECT_TRUE(didUpdate6);

  EXPECT_EQ(cstr_a, trie6.Get("foo").value());
  EXPECT_EQ(cstr_a, trie6.LongestPrefix("foo").val);
  auto matches1 = trie6.findMatchingPrefixes("foo");
  EXPECT_THAT(matches1 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_a));
  EXPECT_EQ(cstr_a, trie6.LongestPrefix("foosball").val);
  auto matches2 = trie6.findMatchingPrefixes("foosball");
  EXPECT_THAT(matches2 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_a));
  EXPECT_EQ(cstr_a, trie6.LongestPrefix("foo/").val);
  auto matches3 = trie6.findMatchingPrefixes("foo/");
  EXPECT_THAT(matches3 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_a));
  EXPECT_EQ(cstr_d, trie6.LongestPrefix("foo/bar").val);
  auto matches4 = trie6.findMatchingPrefixes("foo/bar");
  EXPECT_THAT(matches4 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_a, cstr_d));
  EXPECT_EQ(cstr_d, trie6.LongestPrefix("foo/bar/zzz").val);
  auto matches5 = trie6.findMatchingPrefixes("foo/bar/zzz");
  EXPECT_THAT(matches5 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_a, cstr_d));

  EXPECT_EQ(cstr_b, trie6.Get("bar").value());
  EXPECT_EQ(cstr_b, trie6.LongestPrefix("bar").val);
  auto matches6 = trie6.findMatchingPrefixes("bar");
  EXPECT_THAT(matches6 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_b));
  EXPECT_EQ(cstr_b, trie6.LongestPrefix("baritone").val);
  auto matches7 = trie6.findMatchingPrefixes("baritone");
  EXPECT_THAT(matches7 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_b));
  EXPECT_EQ(cstr_c, trie6.LongestPrefix("barometer").val);
  auto matches8 = trie6.findMatchingPrefixes("barometer");
  EXPECT_THAT(matches8 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_b, cstr_c));

  EXPECT_EQ(cstr_e, trie6.Get("barn").value());
  EXPECT_EQ(cstr_e, trie6.LongestPrefix("barnacle").val);
  auto matches9 = trie6.findMatchingPrefixes("barnacle");
  EXPECT_THAT(matches9 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_b, cstr_e));

  EXPECT_EQ(cstr_f, trie6.Get("barp").value());
  EXPECT_EQ(cstr_f, trie6.LongestPrefix("barpomus").val);
  auto matches10 = trie6.findMatchingPrefixes("barpomus");
  EXPECT_THAT(matches10 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre(cstr_b, cstr_f));

  EXPECT_FALSE(trie6.Get("toto").has_value());
  EXPECT_FALSE(trie6.LongestPrefix("toto").found);
  auto matches11 = trie6.findMatchingPrefixes("toto");
  EXPECT_THAT(matches11 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre());
  EXPECT_FALSE(trie6.Get(" ").has_value());
  EXPECT_FALSE(trie6.LongestPrefix(" ").found);
  auto matches12 = trie6.findMatchingPrefixes(" ");
  EXPECT_THAT(matches12 | testing::Each(testing::Field(&std::pair<std::string, const char*>::second, testing::_)), ElementsAre());
}

TEST(TrieHybrid, VeryDeepTrieDoesNotStackOverflowOnDestructor) {
  TrieHybrid<std::string, const char*> trie;
  const char* cstr_a = "a";

  std::string key_a(20960, 'a');
  auto [trie1, _, didUpdate1] = trie.insert(key_a, cstr_a);
  EXPECT_TRUE(didUpdate1);
  EXPECT_EQ(cstr_a, trie1.Get(key_a).value());
}

} // namespace Common
} // namespace Envoy 