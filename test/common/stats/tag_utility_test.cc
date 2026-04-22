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

} // namespace
} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
