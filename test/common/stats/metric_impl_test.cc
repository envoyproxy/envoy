#include <string>

#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/utility.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class MetricImplTest : public testing::Test {
protected:
  MetricImplTest() : alloc_(symbol_table_), pool_(symbol_table_) {}
  ~MetricImplTest() override { clearStorage(); }

  StatName makeStat(absl::string_view name) { return pool_.add(name); }

  void clearStorage() {
    pool_.clear();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  StatNamePool pool_;
};

// No truncation occurs in the implementation of HeapStatData.
TEST_F(MetricImplTest, NoTags) {
  CounterSharedPtr counter = alloc_.makeCounter(makeStat("counter"), StatName(), {});
  EXPECT_EQ(0, counter->tags().size());
}

TEST_F(MetricImplTest, OneTag) {
  CounterSharedPtr counter = alloc_.makeCounter(makeStat("counter.name.value"), makeStat("counter"),
                                                {{makeStat("name"), makeStat("value")}});
  TagVector tags = counter->tags();
  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("name", tags[0].name_);
  EXPECT_EQ("value", tags[0].value_);
  EXPECT_EQ("counter.name.value", counter->name());
  EXPECT_EQ("counter", counter->tagExtractedName());
  EXPECT_EQ(makeStat("counter"), counter->tagExtractedStatName());
}

TEST_F(MetricImplTest, TwoTagsIterOnce) {
  CounterSharedPtr counter = alloc_.makeCounter(
      makeStat("counter.name.value"), makeStat("counter"),
      {{makeStat("name1"), makeStat("value1")}, {makeStat("name2"), makeStat("value2")}});
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

TEST_F(MetricImplTest, FindTag) {
  CounterSharedPtr counter = alloc_.makeCounter(
      makeStat("counter.name.value"), makeStat("counter"),
      {{makeStat("name1"), makeStat("value1")}, {makeStat("name2"), makeStat("value2")}});
  EXPECT_EQ(makeStat("value1"), Utility::findTag(*counter, makeStat("name1")));
  EXPECT_EQ(makeStat("value2"), Utility::findTag(*counter, makeStat("name2")));
  EXPECT_FALSE(Utility::findTag(*counter, makeStat("name3")));
}

TEST(GaugeHelperTest, AdjustAddsWhenAddIsBigger) {
  MockGauge gauge;
  EXPECT_CALL(gauge, add(3));
  gauge.adjust(5, 2);
}

TEST(GaugeHelperTest, AdjustSubtractsWhenSubIsBigger) {
  MockGauge gauge;
  EXPECT_CALL(gauge, sub(3));
  gauge.adjust(2, 5);
}

} // namespace
} // namespace Stats
} // namespace Envoy
