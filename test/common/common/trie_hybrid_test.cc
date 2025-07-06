#include "source/common/common/radix/trie_hybrid.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Common {

TEST(TrieHybridTest, BasicOperations) {
    TrieHybrid<std::string, std::string> hybrid;
    
    // Test insertion of short keys (<= 8) - should go to trie
    auto [hybrid1, oldVal1, didUpdate1] = hybrid.insert("short", "value1");
    EXPECT_FALSE(didUpdate1);
    EXPECT_EQ(hybrid1.len(), 1);
    
    // Test insertion of long keys (> 8) - should go to radix tree
    auto [hybrid2, oldVal2, didUpdate2] = hybrid1.insert("very_long_key_that_exceeds_eight_characters", "value2");
    EXPECT_FALSE(didUpdate2);
    EXPECT_EQ(hybrid2.len(), 2);
    
    // Test Get operations
    auto result1 = hybrid2.Get("short");
    EXPECT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), "value1");
    
    auto result2 = hybrid2.Get("very_long_key_that_exceeds_eight_characters");
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), "value2");
    
    // Test LongestPrefix
    auto longest1 = hybrid2.LongestPrefix("short");
    EXPECT_TRUE(longest1.found);
    EXPECT_EQ(longest1.key, "short");
    EXPECT_EQ(longest1.val, "value1");
    
    auto longest2 = hybrid2.LongestPrefix("very_long_key_that_exceeds_eight_characters");
    EXPECT_TRUE(longest2.found);
    EXPECT_EQ(longest2.key, "very_long_key_that_exceeds_eight_characters");
    EXPECT_EQ(longest2.val, "value2");
    
    // Test findMatchingPrefixes
    auto matches = hybrid2.findMatchingPrefixes("short");
    EXPECT_EQ(matches.size(), 1);
    EXPECT_EQ(matches[0].first, "short");
    EXPECT_EQ(matches[0].second, "value1");
}

TEST(TrieHybridTest, CrossTreeOperations) {
    TrieHybrid<std::string, std::string> hybrid;
    
    // Insert keys in both trees
    auto [hybrid1, _, __] = hybrid.insert("short", "trie_value");
    auto [hybrid2, ___, ____] = hybrid1.insert("very_long_key_for_radix_tree", "radix_value");
    
    // Test that we can find values regardless of which tree they're in
    auto trie_result = hybrid2.Get("short");
    EXPECT_TRUE(trie_result.has_value());
    EXPECT_EQ(trie_result.value(), "trie_value");
    
    auto radix_result = hybrid2.Get("very_long_key_for_radix_tree");
    EXPECT_TRUE(radix_result.has_value());
    EXPECT_EQ(radix_result.value(), "radix_value");
    
    // Test LongestPrefix searches both trees
    auto longest = hybrid2.LongestPrefix("short");
    EXPECT_TRUE(longest.found);
    EXPECT_EQ(longest.key, "short");
    EXPECT_EQ(longest.val, "trie_value");
    
    auto longest2 = hybrid2.LongestPrefix("very_long_key_for_radix_tree");
    EXPECT_TRUE(longest2.found);
    EXPECT_EQ(longest2.key, "very_long_key_for_radix_tree");
    EXPECT_EQ(longest2.val, "radix_value");
}

} // namespace Common
} // namespace Envoy 