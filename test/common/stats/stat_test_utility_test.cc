#include <string>

#include "common/stats/isolated_store_impl.h"

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatTestUtilityTest : public testing::Test {
protected:
  StatTestUtilityTest()
      : symbol_table_(SymbolTableCreator::initAndMakeSymbolTable(false)), store_(*symbol_table_),
        test_store_(store_), dynamic_(*symbol_table_), symbolic_(*symbol_table_) {}

  SymbolTablePtr symbol_table_;
  IsolatedStoreImpl store_;
  TestUtil::TestStore test_store_;
  StatNameDynamicPool dynamic_;
  StatNamePool symbolic_;
};

TEST_F(StatTestUtilityTest, Counters) {
  store_.counterFromStatName(dynamic_.add("dynamic.stat")).inc();
  store_.counterFromStatName(symbolic_.add("symbolic.stat")).inc();
  EXPECT_EQ(1, test_store_.counter("dynamic.stat").value());
  EXPECT_FALSE(test_store_.findCounter("dynamic.stat2"));
  EXPECT_EQ(1, test_store_.counter("symbolic.stat").value());
  EXPECT_FALSE(test_store_.findCounter("symbolic.stat2"));
}

TEST_F(StatTestUtilityTest, Gauges) {
  store_.counterFromStatName(dynamic_.add("dynamic.stat")).inc();
  store_.counterFromStatName(symbolic_.add("symbolic.stat")).inc();
  EXPECT_EQ(1, test_store_.counter("dynamic.stat").value());
  EXPECT_FALSE(test_store_.findGauge("dynamic.stat2"));
  EXPECT_EQ(1, test_store_.counter("symbolic.stat").value());
  EXPECT_FALSE(test_store_.findGauge("symbolic.stat2"));
}

// TODO(jmarantz): support for histograms will be added when needed as we
// proceed with refactoring. Currently this code does not compile because
// TestUtility does not implement findHistogram.
//
// TEST_F(StatTestUtilityTest, Histograms) {
//   store_.counterFromStatName(dynamic_.add("dynamic.stat")).inc();
//   store_.counterFromStatName(symbolic_.add("symbolic.stat")).inc();
//   EXPECT_EQ(1, test_store_.counter("dynamic.stat").value());
//   EXPECT_FALSE(test_store_.findHistogram("dynamic.stat2"));
//   EXPECT_EQ(1, test_store_.counter("symbolic.stat").value());
//   EXPECT_FALSE(test_store_.findHistogram("symbolic.stat2"));
// }

} // namespace
} // namespace Stats
} // namespace Envoy
