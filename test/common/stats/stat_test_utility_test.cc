#include <string>

#include "common/stats/isolated_store_impl.h"

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatTestUtilityTest : public testing::Test {
protected:
  StatTestUtilityTest() : stat_names_(store_), symbol_table_(store_.symbolTable()) {}

  IsolatedStoreImpl store_;
  TestUtil::StatNames stat_names_;
  SymbolTable& symbol_table_;
};

TEST_F(StatTestUtilityTest, InjectDynamics) {
  StatName hello_world_symbolic = stat_names_.injectDynamics("hello.world");
  EXPECT_EQ("hello.world", symbol_table_.toString(hello_world_symbolic));
  EXPECT_EQ(hello_world_symbolic, stat_names_.injectDynamics("hello.world"));

  // Declaring 'hello' as dynamic gives us the same string representation as
  // above, but a differing StatName.
  StatName hello_world_dynamic1 = stat_names_.injectDynamics("`hello`.world");
  EXPECT_EQ("hello.world", symbol_table_.toString(hello_world_dynamic1));
  EXPECT_NE(hello_world_dynamic1, hello_world_symbolic);

  // Declaring 'world' as dynamic gives us the same string representation as
  // above, but a differing StatName from all the above.
  StatName hello_world_dynamic2 = stat_names_.injectDynamics("hello.`world`");
  EXPECT_EQ("hello.world", symbol_table_.toString(hello_world_dynamic2));
  EXPECT_NE(hello_world_dynamic2, hello_world_dynamic1);
  EXPECT_NE(hello_world_dynamic2, hello_world_symbolic);

  // Ditto for declaring all of "hello.world" as dynamic.
  StatName hello_world_dynamic3 = stat_names_.injectDynamics("`hello.world`");
  EXPECT_EQ("hello.world", symbol_table_.toString(hello_world_dynamic3));
  EXPECT_NE(hello_world_dynamic3, hello_world_dynamic1);
  EXPECT_NE(hello_world_dynamic3, hello_world_dynamic2);
  EXPECT_NE(hello_world_dynamic3, hello_world_symbolic);
}

} // namespace
} // namespace Stats
} // namespace Envoy
