#include "source/common/common/trie_lookup_table.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(TrieLookupTable, AddItems) {
  TrieLookupTable<const char*> trie;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  EXPECT_TRUE(trie.add("foo", cstr_a));
  EXPECT_TRUE(trie.add("bar", cstr_b));
  EXPECT_EQ(cstr_a, trie.find("foo"));
  EXPECT_EQ(cstr_b, trie.find("bar"));

  // overwrite_existing = false
  EXPECT_FALSE(trie.add("foo", cstr_c, false));
  EXPECT_EQ(cstr_a, trie.find("foo"));

  // overwrite_existing = true
  EXPECT_TRUE(trie.add("foo", cstr_c));
  EXPECT_EQ(cstr_c, trie.find("foo"));
}

TEST(TrieLookupTable, LongestPrefix) {
  TrieLookupTable<const char*> trie;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";
  const char* cstr_d = "d";
  const char* cstr_e = "e";
  const char* cstr_f = "f";

  EXPECT_TRUE(trie.add("foo", cstr_a));
  EXPECT_TRUE(trie.add("bar", cstr_b));
  EXPECT_TRUE(trie.add("baro", cstr_c));
  EXPECT_TRUE(trie.add("foo/bar", cstr_d));
  // Verify that prepending and appending branches to a node both work.
  EXPECT_TRUE(trie.add("barn", cstr_e));
  EXPECT_TRUE(trie.add("barp", cstr_f));

  EXPECT_EQ(cstr_a, trie.find("foo"));
  EXPECT_EQ(cstr_a, trie.findLongestPrefix("foo"));
  EXPECT_EQ(cstr_a, trie.findLongestPrefix("foosball"));
  EXPECT_EQ(cstr_a, trie.findLongestPrefix("foo/"));
  EXPECT_EQ(cstr_d, trie.findLongestPrefix("foo/bar"));
  EXPECT_EQ(cstr_d, trie.findLongestPrefix("foo/bar/zzz"));

  EXPECT_EQ(cstr_b, trie.find("bar"));
  EXPECT_EQ(cstr_b, trie.findLongestPrefix("bar"));
  EXPECT_EQ(cstr_b, trie.findLongestPrefix("baritone"));
  EXPECT_EQ(cstr_c, trie.findLongestPrefix("barometer"));

  EXPECT_EQ(cstr_e, trie.find("barn"));
  EXPECT_EQ(cstr_e, trie.findLongestPrefix("barnacle"));

  EXPECT_EQ(cstr_f, trie.find("barp"));
  EXPECT_EQ(cstr_f, trie.findLongestPrefix("barpomus"));

  EXPECT_EQ(nullptr, trie.find("toto"));
  EXPECT_EQ(nullptr, trie.findLongestPrefix("toto"));
  EXPECT_EQ(nullptr, trie.find(" "));
  EXPECT_EQ(nullptr, trie.findLongestPrefix(" "));
}

TEST(TrieLookupTable, VeryDeepTrieDoesNotStackOverflowOnDestructor) {
  TrieLookupTable<const char*> trie;
  const char* cstr_a = "a";

  std::string key_a(20960, 'a');
  EXPECT_TRUE(trie.add(key_a, cstr_a));
  EXPECT_EQ(cstr_a, trie.find(key_a));
}

} // namespace Envoy
