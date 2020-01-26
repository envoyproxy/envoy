#include <string>

#include "common/stats/isolated_store_impl.h"

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatTestUtilityTest : public testing::Test {
protected:
  StatTestUtilityTest() : symbol_table_(SymbolTableCreator::initAndMakeSymbolTable(false)),
                          store_(*symbol_table_), mixed_stat_names_(store_) {}

  SymbolTablePtr symbol_table_;
  IsolatedStoreImpl store_;
  TestUtil::MixedStatNames mixed_stat_names_;
};

TEST_F(StatTestUtilityTest, InjectDynamics) {
  StatName hello_world_symbolic = mixed_stat_names_.injectDynamics("hello.world");
  EXPECT_EQ("hello.world", symbol_table_->toString(hello_world_symbolic));
  EXPECT_EQ(hello_world_symbolic, mixed_stat_names_.injectDynamics("hello.world"));

  // Declaring 'hello' as dynamic gives us the same string representation as
  // above, but a differing StatName.
  StatName hello_world_dynamic1 = mixed_stat_names_.injectDynamics("`hello`.world");
  EXPECT_EQ("hello.world", symbol_table_->toString(hello_world_dynamic1));
  EXPECT_NE(hello_world_dynamic1, hello_world_symbolic);

  // Declaring 'world' as dynamic gives us the same string representation as
  // above, but a differing StatName from all the above.
  StatName hello_world_dynamic2 = mixed_stat_names_.injectDynamics("hello.`world`");
  EXPECT_EQ("hello.world", symbol_table_->toString(hello_world_dynamic2));
  EXPECT_NE(hello_world_dynamic2, hello_world_dynamic1);
  EXPECT_NE(hello_world_dynamic2, hello_world_symbolic);

  // Ditto for declaring all of "hello.world" as dynamic, which also
  // demonstrates that we can embed dots in the dynamic.
  StatName hello_world_dynamic3 = mixed_stat_names_.injectDynamics("`hello.world`");
  EXPECT_EQ("hello.world", symbol_table_->toString(hello_world_dynamic3));
  EXPECT_NE(hello_world_dynamic3, hello_world_dynamic1);
  EXPECT_NE(hello_world_dynamic3, hello_world_dynamic2);
  EXPECT_NE(hello_world_dynamic3, hello_world_symbolic);
}

} // namespace
} // namespace Stats
} // namespace Envoy
