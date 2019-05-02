#include <string>

#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/heap_stat_data.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class MetricImplTest : public testing::Test {
protected:
  MetricImplTest() : alloc_(symbol_table_) {}
  ~MetricImplTest() { clearStorage(); }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, symbol_table_);
  }

  StatName makeStat(absl::string_view name) {
    stat_name_storage_.emplace_back(makeStatStorage(name));
    return stat_name_storage_.back().statName();
  }

  void clearStorage() {
    for (auto& stat_name_storage : stat_name_storage_) {
      stat_name_storage.free(symbol_table_);
    }
    stat_name_storage_.clear();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  FakeSymbolTableImpl symbol_table_;
  HeapStatDataAllocator alloc_;
  std::vector<StatNameStorage> stat_name_storage_;
};

// No truncation occurs in the implementation of HeapStatData.
TEST_F(MetricImplTest, NoTags) {
  CounterSharedPtr counter = alloc_.makeCounter(makeStat("counter"), "", {});
  EXPECT_EQ(0, counter->tags().size());
}

TEST_F(MetricImplTest, OneTag) {
  CounterSharedPtr counter =
      alloc_.makeCounter(makeStat("counter.name.value"), "counter", {{"name", "value"}});
  std::vector<Tag> tags = counter->tags();
  EXPECT_EQ(1, tags.size());
}

TEST_F(MetricImplTest, TwoTagsIterOnce) {
  CounterSharedPtr counter = alloc_.makeCounter(makeStat("counter.name.value"), "counter",
                                                {{"name1", "value1"}, {"name2", "value2"}});
  StatName name1 = makeStat("name1");
  StatName value1 = makeStat("value1");
  int count = 0;
  counter->iterateTagStatNames([&name1, &value1, &count](StatName name, StatName value) -> bool {
    EXPECT_EQ(name1, name);
    EXPECT_EQ(value1, value);
    ++count;
    return false; // Abort the iteration at first tag.
  });
  EXPECT_EQ(1, count);
}

} // namespace
} // namespace Stats
} // namespace Envoy
