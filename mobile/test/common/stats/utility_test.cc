#include "envoy/stats/scope.h"

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/stats/utility.h"

namespace Envoy {
namespace Stats {
namespace Utility {

envoy_stats_tags make_envoy_stats_tags(std::vector<std::pair<std::string, std::string>> pairs) {
  envoy_map_entry* tags =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * pairs.size()));
  envoy_stats_tags new_tags;
  new_tags.length = 0;
  new_tags.entries = tags;

  for (const auto& pair : pairs) {
    envoy_data key = Data::Utility::copyToBridgeData(pair.first);
    envoy_data value = Data::Utility::copyToBridgeData(pair.second);
    new_tags.entries[new_tags.length] = {key, value};
    new_tags.length++;
  }
  return new_tags;
}

TEST(TransformTest, FromEnvoyStatsTagsToStatNameTagVector) {
  Stats::TestUtil::TestSymbolTable symbol_table_;
  StatNameSetPtr stat_name_set_;
  stat_name_set_ = symbol_table_->makeSet("pulse");
  envoy_stats_tags tags = make_envoy_stats_tags({{"os", "android"}, {"flavor", "dev"}});
  Stats::StatNameTagVector tags_vctr = transformToStatNameTagVector(tags, stat_name_set_);
  ASSERT_EQ(tags_vctr.size(), 2);
  ASSERT_EQ(symbol_table_->toString(tags_vctr[0].first), "os");
  ASSERT_EQ(symbol_table_->toString(tags_vctr[0].second), "android");
  ASSERT_EQ(symbol_table_->toString(tags_vctr[1].first), "flavor");
  ASSERT_EQ(symbol_table_->toString(tags_vctr[1].second), "dev");
}

TEST(TransformTest, FromEnvoyStatsTagsToStatNameTagVectorNoTags) {
  Stats::TestUtil::TestSymbolTable symbol_table_;
  StatNameSetPtr stat_name_set_;
  stat_name_set_ = symbol_table_->makeSet("pulse");
  Stats::StatNameTagVector tags_vctr =
      transformToStatNameTagVector(envoy_stats_notags, stat_name_set_);
  ASSERT_EQ(tags_vctr.size(), 0);
}

} // namespace Utility
} // namespace Stats
} // namespace Envoy
