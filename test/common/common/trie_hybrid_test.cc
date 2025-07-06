#include "source/common/common/radix/trie_hybrid.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Common {

TEST(TrieHybridTest, BasicOperations) {
    TrieHybrid<std::string, const void*> hybrid;
    
    // Insert and retrieve a short key
    auto [hybrid1, oldVal1, didUpdate1] = hybrid.insert("short", "value1");
    EXPECT_FALSE(didUpdate1);
    EXPECT_EQ(hybrid1.len(), 0); // Only long keys contribute to len()
    
    // Insert and retrieve a long key
    auto [hybrid2, oldVal2, didUpdate2] = hybrid1.insert("very_long_key_that_exceeds_eight_characters", "value2");
    EXPECT_FALSE(didUpdate2);
    EXPECT_EQ(hybrid2.len(), 1);
    
    // Get operations
    auto result1 = hybrid2.Get("short");
    EXPECT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), static_cast<const void*>("value1"));
    
    auto result2 = hybrid2.Get("very_long_key_that_exceeds_eight_characters");
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), static_cast<const void*>("value2"));
    
    // LongestPrefix only works for long keys
    auto longest1 = hybrid2.LongestPrefix("very_long_key_that_exceeds_eight_characters");
    EXPECT_TRUE(longest1.found);
    EXPECT_EQ(longest1.key, "very_long_key_that_exceeds_eight_characters");
    EXPECT_EQ(longest1.val, static_cast<const void*>("value2"));
    
    // findMatchingPrefixes returns exact match for short key
    auto matches = hybrid2.findMatchingPrefixes("short");
    EXPECT_EQ(matches.size(), 1);
    EXPECT_EQ(matches[0].first, "short");
    EXPECT_EQ(matches[0].second, static_cast<const void*>("value1"));
}

TEST(TrieHybridTest, CrossTreeOperations) {
    TrieHybrid<std::string, const void*> hybrid;
    
    // Insert keys of different lengths
    auto [hybrid1, _, __] = hybrid.insert("short", "trie_value");
    auto [hybrid2, ___, ____] = hybrid1.insert("very_long_key_for_radix_tree", "radix_value");
    
    // Get operations for both keys
    auto trie_result = hybrid2.Get("short");
    EXPECT_TRUE(trie_result.has_value());
    EXPECT_EQ(trie_result.value(), static_cast<const void*>("trie_value"));
    
    auto radix_result = hybrid2.Get("very_long_key_for_radix_tree");
    EXPECT_TRUE(radix_result.has_value());
    EXPECT_EQ(radix_result.value(), static_cast<const void*>("radix_value"));
    
    // LongestPrefix only works for long keys
    auto longest = hybrid2.LongestPrefix("very_long_key_for_radix_tree");
    EXPECT_TRUE(longest.found);
    EXPECT_EQ(longest.key, "very_long_key_for_radix_tree");
    EXPECT_EQ(longest.val, static_cast<const void*>("radix_value"));
    
    // LongestPrefix for short key returns not found
    auto longest2 = hybrid2.LongestPrefix("short");
    EXPECT_FALSE(longest2.found);
}

} // namespace Common
} // namespace Envoy 