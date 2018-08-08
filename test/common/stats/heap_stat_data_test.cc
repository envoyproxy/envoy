#include <string>

#include "common/stats/heap_stat_data.h"
#include "common/stats/stats_options_impl.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

// No truncation occurs in the implementation of HeapStatData.
TEST(HeapStatDataTest, HeapNoTruncate) {
  StatsOptionsImpl stats_options;
  HeapStatDataAllocator alloc;
  const std::string long_string(stats_options.maxNameLength() + 1, 'A');
  HeapStatData* stat{};
  EXPECT_NO_LOGS(stat = alloc.alloc(long_string));
  EXPECT_EQ(stat->key(), long_string);
  alloc.free(*stat);
}

// Note: a similar test using RawStatData* is in test/server/hot_restart_impl_test.cc.
TEST(HeapStatDataTest, HeapAlloc) {
  HeapStatDataAllocator alloc;
  HeapStatData* stat_1 = alloc.alloc("ref_name");
  ASSERT_NE(stat_1, nullptr);
  HeapStatData* stat_2 = alloc.alloc("ref_name");
  ASSERT_NE(stat_2, nullptr);
  HeapStatData* stat_3 = alloc.alloc("not_ref_name");
  ASSERT_NE(stat_3, nullptr);
  EXPECT_EQ(stat_1, stat_2);
  EXPECT_NE(stat_1, stat_3);
  EXPECT_NE(stat_2, stat_3);
  alloc.free(*stat_1);
  alloc.free(*stat_2);
  alloc.free(*stat_3);
}

} // namespace Stats
} // namespace Envoy
