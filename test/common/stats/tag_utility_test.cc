#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

namespace {

class TagUtilityTest : public ::testing::Test {
protected:
  SymbolTable symbol_table_;
  StatNamePool symbolic_pool_{symbol_table_};
  StatNameDynamicPool dynamic_pool_{symbol_table_};
};

TEST_F(TagUtilityTest, Symbolic) {
  StatNameTagVector tags;
  tags.push_back(StatNameTag(symbolic_pool_.add("tag_name"), symbolic_pool_.add("tag_value")));
  TagStatNameJoiner joiner(symbolic_pool_.add("prefix"), symbolic_pool_.add("name"), tags,
                           symbol_table_);
  EXPECT_EQ("prefix.name.tag_name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
}

TEST_F(TagUtilityTest, Dynamic) {
  StatNameTagVector tags;
  tags.push_back(StatNameTag(dynamic_pool_.add("tag_name"), dynamic_pool_.add("tag_value")));
  TagStatNameJoiner joiner(dynamic_pool_.add("prefix"), dynamic_pool_.add("name"), tags,
                           symbol_table_);
  EXPECT_EQ("prefix.name.tag_name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
}

// With no tags and no explicit name, the tag-aware constructor behaves like a plain
// tag_extracted_prefix + tag_extracted_name join.
// Constructor arg order:
// (tag_extracted_prefix, prefix_tags, prefix, tag_extracted_name, name_tags, name, symbol_table).
TEST_F(TagUtilityTest, TagAwareNoTagsNoName) {
  const StatName tag_extracted_prefix = symbolic_pool_.add("prefix");
  TagStatNameJoiner joiner(tag_extracted_prefix, {}, tag_extracted_prefix,
                           symbolic_pool_.add("name"), {}, StatName(), symbol_table_);
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_FALSE(joiner.effectiveTags().has_value());
}

// Without an explicit name, this element's own name_tags are appended (legacy convention).
TEST_F(TagUtilityTest, TagAwareOwnTagsAppended) {
  const StatName tag_extracted_prefix = symbolic_pool_.add("prefix");
  StatNameTagVector name_tags{{symbolic_pool_.add("tag_name"), symbolic_pool_.add("tag_value")}};
  TagStatNameJoiner joiner(tag_extracted_prefix, {}, tag_extracted_prefix,
                           symbolic_pool_.add("name"), StatNameTagSpan(name_tags), StatName(),
                           symbol_table_);
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_EQ("prefix.name.tag_name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  EXPECT_EQ(1, joiner.effectiveTags()->size());
}

// When an explicit name is provided it becomes the flat name (prefix prepended) and own name_tags
// are NOT appended -- the caller controls where the tag value (foo) appears.
TEST_F(TagUtilityTest, TagAwareNameWins) {
  const StatName cluster = symbolic_pool_.add("cluster");
  StatNameTagVector name_tags{{symbolic_pool_.add("cluster_name"), symbolic_pool_.add("foo")}};
  TagStatNameJoiner joiner(cluster, {}, cluster, symbolic_pool_.add("upstream_rq"),
                           StatNameTagSpan(name_tags), symbolic_pool_.add("foo.upstream_rq"),
                           symbol_table_);
  EXPECT_EQ("cluster.upstream_rq", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_EQ("cluster.foo.upstream_rq", symbol_table_.toString(joiner.nameWithTags()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  EXPECT_EQ(1, joiner.effectiveTags()->size());
}

// Inherited (scope-level) prefix_tags are reflected in the prefix already, so they are
// propagated as effective tags but NOT re-appended to the flat name (no double-counting of "foo").
TEST_F(TagUtilityTest, TagAwareInheritedTagsPropagate) {
  const StatName tag_extracted_prefix = symbolic_pool_.add("cluster");
  const StatName prefix = symbolic_pool_.add("cluster.foo");
  StatNameTagVector inherited{{symbolic_pool_.add("cluster_name"), symbolic_pool_.add("foo")}};
  TagStatNameJoiner joiner(tag_extracted_prefix, StatNameTagSpan(inherited), prefix,
                           symbolic_pool_.add("rq"), {}, StatName(), symbol_table_);
  EXPECT_EQ("cluster.rq", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_EQ("cluster.foo.rq", symbol_table_.toString(joiner.nameWithTags()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  EXPECT_EQ(1, joiner.effectiveTags()->size());
}

// Inherited prefix_tags propagate (metadata only) while this element's own name_tags are appended
// once.
TEST_F(TagUtilityTest, TagAwareInheritedPlusOwnTags) {
  const StatName tag_extracted_prefix = symbolic_pool_.add("cluster");
  const StatName prefix = symbolic_pool_.add("cluster.foo");
  StatNameTagVector inherited{{symbolic_pool_.add("cluster_name"), symbolic_pool_.add("foo")}};
  StatNameTagVector own{{symbolic_pool_.add("method"), symbolic_pool_.add("get")}};
  TagStatNameJoiner joiner(tag_extracted_prefix, StatNameTagSpan(inherited), prefix,
                           symbolic_pool_.add("rq"), StatNameTagSpan(own), StatName(),
                           symbol_table_);
  EXPECT_EQ("cluster.rq", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_EQ("cluster.foo.rq.method.get", symbol_table_.toString(joiner.nameWithTags()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  EXPECT_EQ(2, joiner.effectiveTags()->size());
}

} // namespace
} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
