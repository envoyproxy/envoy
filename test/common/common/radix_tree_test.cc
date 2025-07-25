#include "source/common/common/radix_tree.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;

namespace Envoy {

TEST(RadixTree, AddItems) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  EXPECT_TRUE(radixtree.add(std::string("foo"), cstr_a));
  EXPECT_TRUE(radixtree.add(std::string("bar"), cstr_b));
  EXPECT_EQ(cstr_a, radixtree.find("foo"));
  EXPECT_EQ(cstr_b, radixtree.find("bar"));

  // overwrite_existing = false
  EXPECT_FALSE(radixtree.add(std::string("foo"), cstr_c, false));
  EXPECT_EQ(cstr_a, radixtree.find("foo"));

  // overwrite_existing = true
  EXPECT_TRUE(radixtree.add(std::string("foo"), cstr_c));
  EXPECT_EQ(cstr_c, radixtree.find("foo"));
}

TEST(RadixTree, LongestPrefix) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";
  const char* cstr_d = "d";
  const char* cstr_e = "e";
  const char* cstr_f = "f";

  EXPECT_TRUE(radixtree.add(std::string("foo/bar"), cstr_d));
  EXPECT_TRUE(radixtree.add(std::string("foo"), cstr_a));
  // Verify that prepending and appending branches to a node both work.
  EXPECT_TRUE(radixtree.add(std::string("barn"), cstr_e));
  EXPECT_TRUE(radixtree.add(std::string("barp"), cstr_f));
  EXPECT_TRUE(radixtree.add(std::string("bar"), cstr_b));
  EXPECT_TRUE(radixtree.add(std::string("baro"), cstr_c));

  EXPECT_EQ(cstr_a, radixtree.find("foo"));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("foo"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("foo"), ElementsAre(cstr_a));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("foosball"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("foosball"), ElementsAre(cstr_a));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("foo/"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("foo/"), ElementsAre(cstr_a));
  EXPECT_EQ(cstr_d, radixtree.findLongestPrefix("foo/bar"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("foo/bar"), ElementsAre(cstr_a, cstr_d));
  EXPECT_EQ(cstr_d, radixtree.findLongestPrefix("foo/bar/zzz"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("foo/bar/zzz"), ElementsAre(cstr_a, cstr_d));

  EXPECT_EQ(cstr_b, radixtree.find("bar"));
  EXPECT_EQ(cstr_b, radixtree.findLongestPrefix("bar"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("bar"), ElementsAre(cstr_b));
  EXPECT_EQ(cstr_b, radixtree.findLongestPrefix("baritone"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("baritone"), ElementsAre(cstr_b));
  EXPECT_EQ(cstr_c, radixtree.findLongestPrefix("barometer"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("barometer"), ElementsAre(cstr_b, cstr_c));

  EXPECT_EQ(cstr_e, radixtree.find("barn"));
  EXPECT_EQ(cstr_e, radixtree.findLongestPrefix("barnacle"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("barnacle"), ElementsAre(cstr_b, cstr_e));

  EXPECT_EQ(cstr_f, radixtree.find("barp"));
  EXPECT_EQ(cstr_f, radixtree.findLongestPrefix("barpomus"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("barpomus"), ElementsAre(cstr_b, cstr_f));

  EXPECT_EQ(nullptr, radixtree.find("toto"));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix("toto"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("toto"), ElementsAre());
  EXPECT_EQ(nullptr, radixtree.find(" "));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix(" "));
  EXPECT_THAT(radixtree.findMatchingPrefixes(" "), ElementsAre());
}

TEST(RadixTree, VeryDeepRadixTreeDoesNotStackOverflowOnDestructor) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";

  std::string key_a(20960, 'a');
  EXPECT_TRUE(radixtree.add(key_a, cstr_a));
  EXPECT_EQ(cstr_a, radixtree.find(key_a));
}

TEST(RadixTree, RadixTreeSpecificTests) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";
  const char* cstr_d = "d";

  // Test radix tree compression
  EXPECT_TRUE(radixtree.add(std::string("test"), cstr_a));
  EXPECT_TRUE(radixtree.add(std::string("testing"), cstr_b));
  EXPECT_TRUE(radixtree.add(std::string("tester"), cstr_c));
  EXPECT_TRUE(radixtree.add(std::string("tested"), cstr_d));

  EXPECT_EQ(cstr_a, radixtree.find("test"));
  EXPECT_EQ(cstr_b, radixtree.find("testing"));
  EXPECT_EQ(cstr_c, radixtree.find("tester"));
  EXPECT_EQ(cstr_d, radixtree.find("tested"));

  // Test prefix matching
  EXPECT_THAT(radixtree.findMatchingPrefixes("test"), ElementsAre(cstr_a));
  EXPECT_THAT(radixtree.findMatchingPrefixes("testing"), ElementsAre(cstr_a, cstr_b));
  EXPECT_THAT(radixtree.findMatchingPrefixes("tester"), ElementsAre(cstr_a, cstr_c));
  EXPECT_THAT(radixtree.findMatchingPrefixes("tested"), ElementsAre(cstr_a, cstr_d));

  // Test longest prefix
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("test"));
  EXPECT_EQ(cstr_b, radixtree.findLongestPrefix("testing"));
  EXPECT_EQ(cstr_c, radixtree.findLongestPrefix("tester"));
  EXPECT_EQ(cstr_d, radixtree.findLongestPrefix("tested"));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("testx"));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix("tex"));
}

TEST(RadixTree, EmptyAndSingleNode) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";

  // Test empty radixtree
  EXPECT_EQ(nullptr, radixtree.find("anything"));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix("anything"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("anything"), ElementsAre());

  // Test single node
  EXPECT_TRUE(radixtree.add(std::string("a"), cstr_a));
  EXPECT_EQ(cstr_a, radixtree.find("a"));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("a"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("a"), ElementsAre(cstr_a));
  EXPECT_EQ(nullptr, radixtree.find("b"));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix("b"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("b"), ElementsAre());
}

TEST(RadixTree, InsertAndFindEdgeCases) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";
  const char* cstr_d = "d";

  // Test empty string
  EXPECT_TRUE(radixtree.add(std::string(""), cstr_a));
  EXPECT_EQ(cstr_a, radixtree.find(""));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix(""));
  EXPECT_THAT(radixtree.findMatchingPrefixes(""), ElementsAre(cstr_a));

  // Test single character
  EXPECT_TRUE(radixtree.add(std::string("x"), cstr_b));
  EXPECT_EQ(cstr_b, radixtree.find("x"));
  EXPECT_EQ(cstr_b, radixtree.findLongestPrefix("x"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("x"), ElementsAre(cstr_a, cstr_b));

  // Test very long string
  std::string long_key(1000, 'a');
  EXPECT_TRUE(radixtree.add(long_key, cstr_c));
  EXPECT_EQ(cstr_c, radixtree.find(long_key));
  EXPECT_EQ(cstr_c, radixtree.findLongestPrefix(long_key));
  EXPECT_THAT(radixtree.findMatchingPrefixes(long_key), ElementsAre(cstr_a, cstr_c));

  // Test special characters
  EXPECT_TRUE(radixtree.add(std::string("test/key"), cstr_d));
  EXPECT_EQ(cstr_d, radixtree.find("test/key"));
  EXPECT_EQ(cstr_d, radixtree.findLongestPrefix("test/key"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("test/key"), ElementsAre(cstr_a, cstr_d));

  // Test non-existent keys
  EXPECT_EQ(nullptr, radixtree.find("nonexistent"));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("nonexistent"));
  EXPECT_THAT(radixtree.findMatchingPrefixes("nonexistent"), ElementsAre(cstr_a));
}

TEST(RadixTree, InsertAndFindComplexScenarios) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";
  const char* cstr_d = "d";
  const char* cstr_e = "e";
  const char* cstr_f = "f";

  // Test overlapping prefixes
  EXPECT_TRUE(radixtree.add(std::string("test"), cstr_a));
  EXPECT_TRUE(radixtree.add(std::string("testing"), cstr_b));
  EXPECT_TRUE(radixtree.add(std::string("tester"), cstr_c));
  EXPECT_TRUE(radixtree.add(std::string("tested"), cstr_d));

  // Verify all can be found
  EXPECT_EQ(cstr_a, radixtree.find("test"));
  EXPECT_EQ(cstr_b, radixtree.find("testing"));
  EXPECT_EQ(cstr_c, radixtree.find("tester"));
  EXPECT_EQ(cstr_d, radixtree.find("tested"));

  // Test prefix matching
  EXPECT_THAT(radixtree.findMatchingPrefixes("test"), ElementsAre(cstr_a));
  EXPECT_THAT(radixtree.findMatchingPrefixes("testing"), ElementsAre(cstr_a, cstr_b));
  EXPECT_THAT(radixtree.findMatchingPrefixes("tester"), ElementsAre(cstr_a, cstr_c));
  EXPECT_THAT(radixtree.findMatchingPrefixes("tested"), ElementsAre(cstr_a, cstr_d));

  // Test longest prefix
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("test"));
  EXPECT_EQ(cstr_b, radixtree.findLongestPrefix("testing"));
  EXPECT_EQ(cstr_c, radixtree.findLongestPrefix("tester"));
  EXPECT_EQ(cstr_d, radixtree.findLongestPrefix("tested"));
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("testx"));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix("tex"));

  // Test branching scenarios
  EXPECT_TRUE(radixtree.add(std::string("hello"), cstr_e));
  EXPECT_TRUE(radixtree.add(std::string("world"), cstr_f));

  EXPECT_EQ(cstr_e, radixtree.find("hello"));
  EXPECT_EQ(cstr_f, radixtree.find("world"));
  EXPECT_EQ(cstr_e, radixtree.findLongestPrefix("hello"));
  EXPECT_EQ(cstr_f, radixtree.findLongestPrefix("world"));
}

TEST(RadixTree, InsertAndFindOverwriteBehavior) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  // Test overwrite_existing = true (default)
  EXPECT_TRUE(radixtree.add(std::string("key"), cstr_a));
  EXPECT_EQ(cstr_a, radixtree.find("key"));

  EXPECT_TRUE(radixtree.add(std::string("key"), cstr_b));
  EXPECT_EQ(cstr_b, radixtree.find("key"));

  // Test overwrite_existing = false
  EXPECT_FALSE(radixtree.add(std::string("key"), cstr_c, false));
  EXPECT_EQ(cstr_b, radixtree.find("key")); // Should still be cstr_b

  // Test overwrite_existing = true explicitly
  EXPECT_TRUE(radixtree.add(std::string("key"), cstr_c, true));
  EXPECT_EQ(cstr_c, radixtree.find("key"));
}

TEST(RadixTree, InsertAndFindDeepNesting) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  // Test deep nesting
  EXPECT_TRUE(radixtree.add(std::string("a/b/c/d/e/f"), cstr_a));
  EXPECT_TRUE(radixtree.add(std::string("a/b/c/d/e/g"), cstr_b));
  EXPECT_TRUE(radixtree.add(std::string("a/b/c/d/e/h"), cstr_c));

  EXPECT_EQ(cstr_a, radixtree.find("a/b/c/d/e/f"));
  EXPECT_EQ(cstr_b, radixtree.find("a/b/c/d/e/g"));
  EXPECT_EQ(cstr_c, radixtree.find("a/b/c/d/e/h"));

  // Test prefix matching on deep paths
  EXPECT_THAT(radixtree.findMatchingPrefixes("a/b/c/d/e/f"), ElementsAre(cstr_a));
  EXPECT_THAT(radixtree.findMatchingPrefixes("a/b/c/d/e/g"), ElementsAre(cstr_b));
  EXPECT_THAT(radixtree.findMatchingPrefixes("a/b/c/d/e/h"), ElementsAre(cstr_c));

  // Test longest prefix on deep paths
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("a/b/c/d/e/f"));
  EXPECT_EQ(cstr_b, radixtree.findLongestPrefix("a/b/c/d/e/g"));
  EXPECT_EQ(cstr_c, radixtree.findLongestPrefix("a/b/c/d/e/h"));
}

TEST(RadixTree, InsertAndFindMixedLengths) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";
  const char* cstr_d = "d";

  // Test mixed length keys
  EXPECT_TRUE(radixtree.add(std::string("a"), cstr_a));
  EXPECT_TRUE(radixtree.add(std::string("aa"), cstr_b));
  EXPECT_TRUE(radixtree.add(std::string("aaa"), cstr_c));
  EXPECT_TRUE(radixtree.add(std::string("aaaa"), cstr_d));

  EXPECT_EQ(cstr_a, radixtree.find("a"));
  EXPECT_EQ(cstr_b, radixtree.find("aa"));
  EXPECT_EQ(cstr_c, radixtree.find("aaa"));
  EXPECT_EQ(cstr_d, radixtree.find("aaaa"));

  // Test prefix matching
  EXPECT_THAT(radixtree.findMatchingPrefixes("a"), ElementsAre(cstr_a));
  EXPECT_THAT(radixtree.findMatchingPrefixes("aa"), ElementsAre(cstr_a, cstr_b));
  EXPECT_THAT(radixtree.findMatchingPrefixes("aaa"), ElementsAre(cstr_a, cstr_b, cstr_c));
  EXPECT_THAT(radixtree.findMatchingPrefixes("aaaa"), ElementsAre(cstr_a, cstr_b, cstr_c, cstr_d));

  // Test longest prefix
  EXPECT_EQ(cstr_a, radixtree.findLongestPrefix("a"));
  EXPECT_EQ(cstr_b, radixtree.findLongestPrefix("aa"));
  EXPECT_EQ(cstr_c, radixtree.findLongestPrefix("aaa"));
  EXPECT_EQ(cstr_d, radixtree.findLongestPrefix("aaaa"));
  EXPECT_EQ(cstr_d, radixtree.findLongestPrefix("aaaaa"));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix("b"));
}

TEST(RadixTree, InsertAndFindSpecialCharacters) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  // Test special characters
  EXPECT_TRUE(radixtree.add(std::string("test-key"), cstr_a));
  EXPECT_TRUE(radixtree.add(std::string("test_key"), cstr_b));
  EXPECT_TRUE(radixtree.add(std::string("test.key"), cstr_c));

  EXPECT_EQ(cstr_a, radixtree.find("test-key"));
  EXPECT_EQ(cstr_b, radixtree.find("test_key"));
  EXPECT_EQ(cstr_c, radixtree.find("test.key"));

  // Test with spaces
  EXPECT_TRUE(radixtree.add(std::string("test key"), cstr_a));
  EXPECT_EQ(cstr_a, radixtree.find("test key"));

  // Test with numbers
  EXPECT_TRUE(radixtree.add(std::string("test123"), cstr_b));
  EXPECT_EQ(cstr_b, radixtree.find("test123"));
}

TEST(RadixTree, InsertAndFindBooleanInterface) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";

  // Test boolean find interface
  const char* result;

  result = radixtree.find("nonexistent");
  EXPECT_EQ(nullptr, result);

  EXPECT_TRUE(radixtree.add(std::string("key"), cstr_a));
  result = radixtree.find("key");
  EXPECT_EQ(cstr_a, result);

  EXPECT_TRUE(radixtree.add(std::string("key"), cstr_b));
  result = radixtree.find("key");
  EXPECT_EQ(cstr_b, result);

  // Test with empty string
  EXPECT_TRUE(radixtree.add(std::string(""), cstr_a));
  result = radixtree.find("");
  EXPECT_EQ(cstr_a, result);
}

TEST(RadixTree, BasicFunctionality) {
  RadixTree<const char*> radixtree;
  const char* cstr_a = "a";
  const char* cstr_b = "b";

  // Test simple insertion
  EXPECT_TRUE(radixtree.add(std::string("test"), cstr_a));
  EXPECT_EQ(cstr_a, radixtree.find("test"));

  // Test second insertion
  EXPECT_TRUE(radixtree.add(std::string("hello"), cstr_b));
  EXPECT_EQ(cstr_b, radixtree.find("hello"));
  EXPECT_EQ(cstr_a, radixtree.find("test")); // Make sure first one still works
}

TEST(RadixTree, StringOperations) {
  RadixTree<const char*> radixtree;
  const char* value_a = "value_a";
  const char* value_b = "value_b";
  const char* value_c = "value_c";
  const char* value_d = "value_d";

  // Test string operations with various scenarios.
  EXPECT_TRUE(radixtree.add("test", value_a));
  EXPECT_TRUE(radixtree.add("testing", value_b));
  EXPECT_TRUE(radixtree.add("hello", value_c));
  EXPECT_TRUE(radixtree.add("world", value_d));

  // Verify all insertions work correctly.
  EXPECT_EQ(value_a, radixtree.find("test"));
  EXPECT_EQ(value_b, radixtree.find("testing"));
  EXPECT_EQ(value_c, radixtree.find("hello"));
  EXPECT_EQ(value_d, radixtree.find("world"));

  // Test prefix matching.
  EXPECT_EQ(value_a, radixtree.findLongestPrefix("test"));
  EXPECT_EQ(value_b, radixtree.findLongestPrefix("testing"));
  EXPECT_EQ(value_a, radixtree.findLongestPrefix("test_other"));
  EXPECT_EQ(nullptr, radixtree.findLongestPrefix("xyz"));

  // Test that prefix matching works correctly.
  EXPECT_THAT(radixtree.findMatchingPrefixes("testing"), ElementsAre(value_a, value_b));
  EXPECT_THAT(radixtree.findMatchingPrefixes("test"), ElementsAre(value_a));
}

TEST(RadixTree, PerformanceCharacteristics) {
  RadixTree<int> radixtree;

  // Test with a larger number of keys to verify performance characteristics.
  const size_t num_keys = 100; // Reduced for clearer testing
  std::vector<std::string> keys;
  keys.reserve(num_keys);

  // Generate keys with common prefixes to test radix tree compression.
  for (size_t i = 0; i < num_keys; ++i) {
    keys.push_back("prefix_" + std::to_string(i));
  }

  // Insert all keys.
  for (size_t i = 0; i < keys.size(); ++i) {
    int value = static_cast<int>(i + 1);
    EXPECT_TRUE(radixtree.add(keys[i], value));
  }

  // Verify all keys can be found.
  for (size_t i = 0; i < keys.size(); ++i) {
    int expected_value = static_cast<int>(i + 1);
    EXPECT_EQ(expected_value, radixtree.find(keys[i]));
  }

  // Test prefix matching: "prefix_50_extra" should match "prefix_5" and "prefix_50".
  auto prefix_matches = radixtree.findMatchingPrefixes("prefix_50_extra");
  EXPECT_GE(prefix_matches.size(), 1); // Should match at least "prefix_50"

  // Test longest prefix match.
  int longest_match = radixtree.findLongestPrefix("prefix_50_extra");
  EXPECT_EQ(51, longest_match); // prefix_50 + 1

  // Test non-matching prefix.
  EXPECT_EQ(0, radixtree.findLongestPrefix("different_prefix")); // int default value is 0
}

} // namespace Envoy
