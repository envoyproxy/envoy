#include <string>

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatTestUtilityTest : public testing::Test {
protected:
  StatTestUtilityTest()
      : test_store_(symbol_table_), test_scope_(test_store_.rootScope()), dynamic_(symbol_table_),
        symbolic_(symbol_table_) {}

  SymbolTableImpl symbol_table_;
  TestUtil::TestStore test_store_;
  ScopeSharedPtr test_scope_;
  StatNameDynamicPool dynamic_;
  StatNamePool symbolic_;
};

TEST_F(StatTestUtilityTest, Counters) {
  test_scope_->counterFromStatName(dynamic_.add("dynamic.stat")).inc();
  test_scope_->counterFromStatName(symbolic_.add("symbolic.stat")).inc();
  EXPECT_EQ(1, test_store_.findCounterByString("dynamic.stat").value().get().value());
  EXPECT_FALSE(test_store_.findCounterByString("dynamic.stat2"));
  EXPECT_EQ(1, test_store_.findCounterByString("symbolic.stat").value().get().value());
  EXPECT_FALSE(test_store_.findCounterByString("symbolic.stat2"));
}

TEST_F(StatTestUtilityTest, Gauges) {
  test_scope_->gaugeFromStatName(dynamic_.add("dynamic.stat"), Gauge::ImportMode::Accumulate).inc();
  test_scope_->gaugeFromStatName(symbolic_.add("symbolic.stat"), Gauge::ImportMode::Accumulate)
      .inc();
  EXPECT_EQ(1, test_store_.findGaugeByString("dynamic.stat").value().get().value());
  EXPECT_FALSE(test_store_.findGaugeByString("dynamic.stat2"));
  EXPECT_EQ(1, test_store_.findGaugeByString("symbolic.stat").value().get().value());
  EXPECT_FALSE(test_store_.findGaugeByString("symbolic.stat2"));
}

TEST_F(StatTestUtilityTest, Histograms) {
  test_scope_->histogramFromStatName(dynamic_.add("dynamic.stat"), Histogram::Unit::Milliseconds)
      .recordValue(1);
  test_scope_->histogramFromStatName(symbolic_.add("symbolic.stat"), Histogram::Unit::Milliseconds)
      .recordValue(1);
  EXPECT_EQ(Histogram::Unit::Milliseconds,
            test_store_.findHistogramByString("dynamic.stat").value().get().unit());
  EXPECT_FALSE(test_store_.findHistogramByString("dynamic.stat2"));
  EXPECT_EQ(Histogram::Unit::Milliseconds,
            test_store_.findHistogramByString("symbolic.stat").value().get().unit());
  EXPECT_FALSE(test_store_.findHistogramByString("symbolic.stat2"));
}

TEST_F(StatTestUtilityTest, CountersWithTags) {
  StatNameTagVector dynamic_tags = {
      {dynamic_.add("dynamic_tag_name"), dynamic_.add("dynamic_tag_value")}};
  test_scope_->counterFromStatNameWithTags(dynamic_.add("dynamic.stat"), dynamic_tags).inc();
  StatNameTagVector symbolic_tags = {
      {symbolic_.add("symbolic_tag_name"), symbolic_.add("symbolic_tag_value")}};
  test_scope_->counterFromStatNameWithTags(symbolic_.add("symbolic.stat"), symbolic_tags).inc();
  EXPECT_EQ(1, test_store_.findCounterByString("dynamic.stat.dynamic_tag_name.dynamic_tag_value")
                   .value()
                   .get()
                   .value());
  EXPECT_EQ(1, test_store_.findCounterByString("symbolic.stat.symbolic_tag_name.symbolic_tag_value")
                   .value()
                   .get()
                   .value());
}

} // namespace
} // namespace Stats
} // namespace Envoy
