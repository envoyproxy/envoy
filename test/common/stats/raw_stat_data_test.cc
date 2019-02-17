#include <string>

#include "common/stats/raw_stat_data.h"
#include "common/stats/stats_options_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class RawStatDataTest : public testing::Test {
public:
  RawStatDataTest() : allocator_(stats_options_) {}

  StatsOptionsImpl stats_options_;
  TestAllocator allocator_; // This is RawStatDataAllocator with some size settings.
};

// Note: a similar test using HeapStatData* is in heap_stat_data_test.cc.
TEST_F(RawStatDataTest, RawTruncate) {
  const std::string long_string(stats_options_.maxNameLength() + 1, 'A');
  RawStatData* stat{};
  EXPECT_LOG_CONTAINS("warning", " is too long ", stat = allocator_.alloc(long_string));
  EXPECT_NE(stat->key(), long_string);

  // If I add more bytes to the key, I'll get the same allocation back
  // due to the truncated map lookup.
  EXPECT_EQ(stat, allocator_.alloc(long_string + " ignored"));

  allocator_.free(*stat);
  allocator_.free(*stat); // Have to free it twice as second allocation bumped ref-count.
}

// Note: a similar test using HeapStatData* is in heap_stat_data_test.cc.
TEST_F(RawStatDataTest, RawAlloc) {
  Stats::RawStatData* stat_1 = allocator_.alloc("ref_name");
  ASSERT_NE(stat_1, nullptr);
  Stats::RawStatData* stat_2 = allocator_.alloc("ref_name");
  ASSERT_NE(stat_2, nullptr);
  Stats::RawStatData* stat_3 = allocator_.alloc("not_ref_name");
  ASSERT_NE(stat_3, nullptr);
  EXPECT_EQ(stat_1, stat_2);
  EXPECT_NE(stat_1, stat_3);
  EXPECT_NE(stat_2, stat_3);
  allocator_.free(*stat_1);
  allocator_.free(*stat_2);
  allocator_.free(*stat_3);
}

} // namespace Stats
} // namespace Envoy
