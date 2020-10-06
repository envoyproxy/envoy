#include <string>

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatTestUtilityTest : public testing::Test {
protected:
  StatTestUtilityTest()
      : test_store_(symbol_table_), dynamic_(symbol_table_), symbolic_(symbol_table_) {}

  SymbolTableImpl symbol_table_;
  TestUtil::TestStore test_store_;
  StatNameDynamicPool dynamic_;
  StatNamePool symbolic_;
};

TEST_F(StatTestUtilityTest, Counters) {
  test_store_.counterFromStatName(dynamic_.add("dynamic.stat")).inc();
  test_store_.counterFromStatName(symbolic_.add("symbolic.stat")).inc();
  EXPECT_EQ(1, test_store_.counter("dynamic.stat").value());
  EXPECT_FALSE(test_store_.findCounterByString("dynamic.stat2"));
  EXPECT_EQ(1, test_store_.counter("symbolic.stat").value());
  EXPECT_FALSE(test_store_.findCounterByString("symbolic.stat2"));
}

TEST_F(StatTestUtilityTest, Gauges) {
  test_store_.counterFromStatName(dynamic_.add("dynamic.stat")).inc();
  test_store_.counterFromStatName(symbolic_.add("symbolic.stat")).inc();
  EXPECT_EQ(1, test_store_.counter("dynamic.stat").value());
  EXPECT_FALSE(test_store_.findGaugeByString("dynamic.stat2"));
  EXPECT_EQ(1, test_store_.counter("symbolic.stat").value());
  EXPECT_FALSE(test_store_.findGaugeByString("symbolic.stat2"));
}

TEST_F(StatTestUtilityTest, Histograms) {
  test_store_.counterFromStatName(dynamic_.add("dynamic.stat")).inc();
  test_store_.counterFromStatName(symbolic_.add("symbolic.stat")).inc();
  EXPECT_EQ(1, test_store_.counter("dynamic.stat").value());
  EXPECT_FALSE(test_store_.findHistogramByString("dynamic.stat2"));
  EXPECT_EQ(1, test_store_.counter("symbolic.stat").value());
  EXPECT_FALSE(test_store_.findHistogramByString("symbolic.stat2"));
}

} // namespace
} // namespace Stats
} // namespace Envoy
