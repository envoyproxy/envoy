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
        lookup_context_(store_) {}

  SymbolTablePtr symbol_table_;
  IsolatedStoreImpl store_;
  TestUtil::StatNameLookupContext lookup_context_;
};

TEST_F(StatTestUtilityTest, InjectDynamics) {
  StatNameDynamicPool dynamic(*symbol_table_);
  StatNamePool pool(*symbol_table_);

  store_.counterFromStatName(dynamic.add("dynamic.stat")).inc();
  store_.counterFromStatName(pool.add("symbolic.stat")).inc();
  EXPECT_EQ(1, lookup_context_.counter("dynamic.stat").value());
  EXPECT_FALSE(lookup_context_.findCounter("dynamic.stat2"));
  EXPECT_EQ(1, lookup_context_.counter("symbolic.stat").value());
  EXPECT_FALSE(lookup_context_.findCounter("symbolic.stat2"));
}

} // namespace
} // namespace Stats
} // namespace Envoy
