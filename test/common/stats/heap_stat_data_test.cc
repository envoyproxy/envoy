#include <string>

#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/heap_stat_data.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class HeapStatDataTest : public testing::Test {
protected:
  HeapStatDataTest() : alloc_(symbol_table_), pool_(symbol_table_) {}
  ~HeapStatDataTest() { clearStorage(); }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, symbol_table_);
  }

  StatName makeStat(absl::string_view name) { return pool_.add(name); }

  void clearStorage() {
    pool_.clear();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  FakeSymbolTableImpl symbol_table_;
  HeapStatDataAllocator alloc_;
  StatNamePool pool_;
};

// No truncation occurs in the implementation of HeapStatData.
TEST_F(HeapStatDataTest, HeapNoTruncate) {
  const std::string long_string(128, 'A');
  StatName stat_name = makeStat(long_string);
  HeapStatData* stat{};
  EXPECT_NO_LOGS(stat = &alloc_.alloc(stat_name));
  EXPECT_EQ(stat->statName(), stat_name);
  alloc_.free(*stat);
};

TEST_F(HeapStatDataTest, HeapAlloc) {
  HeapStatData* stat_1 = &alloc_.alloc(makeStat("ref_name"));
  ASSERT_NE(stat_1, nullptr);
  HeapStatData* stat_2 = &alloc_.alloc(makeStat("ref_name"));
  ASSERT_NE(stat_2, nullptr);
  HeapStatData* stat_3 = &alloc_.alloc(makeStat("not_ref_name"));
  ASSERT_NE(stat_3, nullptr);
  EXPECT_EQ(stat_1, stat_2);
  EXPECT_NE(stat_1, stat_3);
  EXPECT_NE(stat_2, stat_3);
  alloc_.free(*stat_1);
  alloc_.free(*stat_2);
  alloc_.free(*stat_3);
}

} // namespace
} // namespace Stats
} // namespace Envoy
