#include <string>

#include "common/stats/symbol_table_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class SymbolTableTest : public testing::Test {
public:
  SymbolTableTest() {}

  SymbolTableImpl table_;
};

TEST_F(SymbolTableTest, TestArbitrarySymbolRoundtrip) {
  std::vector<std::string> stat_names = {"", " ", "  ", ",", "\t", "$", "%", "`", "."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, table_.decode(table_.encode(stat_name)));
  }
}

TEST_F(SymbolTableTest, TestUnusualDelimitersRoundtrip) {
  std::vector<std::string> stat_names = {".",    "..",    "...",    "foo",    "foo.",
                                         ".foo", ".foo.", ".foo..", "..foo.", "..foo.."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, table_.decode(table_.encode(stat_name)));
  }
}

TEST_F(SymbolTableTest, TestSuccessfulDoubleLookup) {
  auto vec_1 = table_.encode("foo.bar.baz");
  auto vec_2 = table_.encode("foo.bar.baz");
  EXPECT_EQ(vec_1, vec_2);
}

TEST_F(SymbolTableTest, TestSuccessfulDecode) {
  std::string stat_name = "foo.bar.baz";
  auto vec_1 = table_.encode(stat_name);
  auto vec_2 = table_.encode(stat_name);
  EXPECT_EQ(table_.decode(vec_1), table_.decode(vec_2));
  EXPECT_EQ(table_.decode(vec_1), stat_name);
}

TEST_F(SymbolTableTest, TestDifferentStats) {
  auto vec_1 = table_.encode("foo.bar");
  auto vec_2 = table_.encode("bar.foo");
  EXPECT_NE(vec_1, vec_2);
}

TEST_F(SymbolTableTest, TestSymbolConsistency) {
  auto vec_1 = table_.encode("foo.bar");
  auto vec_2 = table_.encode("bar.foo");
  // We expect the encoding of "foo" in one context to be the same as another.
  EXPECT_EQ(vec_1[0], vec_2[1]);
  EXPECT_EQ(vec_2[0], vec_1[1]);
}

TEST_F(SymbolTableTest, TestBadLookups) {
  // For symbols (in this case 0, 1, and 2) which don't exist in the table_, we expect decoding them
  // to return the empty string ("") instead of crashing or segfaulting.
  EXPECT_EQ("", table_.decode({0}));
  EXPECT_EQ(".", table_.decode({0, 1}));
  EXPECT_EQ("..", table_.decode({0, 1, 2}));
}

TEST_F(SymbolTableTest, TestBadFrees) {
  // ::free() returns a bool denoting whether or not the free operation was successful.
  // Expected to be true for valid symbols, false for invalid symbols.
  EXPECT_FALSE(table_.free({0}));
  auto symbol_vec = table_.encode("example.stat");
  EXPECT_TRUE(table_.free(symbol_vec));
  EXPECT_FALSE(table_.free(symbol_vec));
}

// Even though the symbol table does manual reference counting, curr_counter_ is monotonically
// increasing. So encoding "foo", freeing the sole stat containing "foo", and then re-encoding "foo"
// will produce a different symbol each time.
TEST_F(SymbolTableTest, TestNewValueAfterFree) {
  {
    auto vec_1 = table_.encode("foo");
    EXPECT_TRUE(table_.free(vec_1));
    auto vec_2 = table_.encode("foo");
    EXPECT_NE(vec_1, vec_2);
  }

  {
    // This should hold true for components as well. Since "foo" persists even when "foo.bar" is
    // freed, we expect both instances of "foo" to have the same symbol, but each instance of "bar"
    // to have a different symbol.
    auto vec_foo = table_.encode("foo");
    auto vec_foobar_1 = table_.encode("foo.bar");
    EXPECT_TRUE(table_.free(vec_foobar_1));
    auto vec_foobar_2 = table_.encode("foo.bar");
    EXPECT_EQ(vec_foobar_1[0], vec_foobar_2[0]); // Both "foo" components have the same symbol,
    EXPECT_NE(vec_foobar_1[1], vec_foobar_2[1]); // but the two "bar" components do not.
  }
}

TEST_F(SymbolTableTest, TestShrinkingExpectation) {
  // We expect that as we free stat names, the memory used to store those underlying symbols will be
  // freed.
  // ::size() is a public function, but should only be used for testing.
  size_t table_size_0 = table_.size();

  auto vec_a = table_.encode("a");
  size_t table_size_1 = table_.size();

  auto vec_aa = table_.encode("a.a");
  EXPECT_EQ(table_size_1, table_.size());

  auto vec_ab = table_.encode("a.b");
  size_t table_size_2 = table_.size();

  auto vec_ac = table_.encode("a.c");
  size_t table_size_3 = table_.size();

  auto vec_acd = table_.encode("a.c.d");
  size_t table_size_4 = table_.size();

  auto vec_ace = table_.encode("a.c.e");
  size_t table_size_5 = table_.size();
  EXPECT_GE(table_size_5, table_size_4);

  EXPECT_TRUE(table_.free(vec_ace));
  EXPECT_EQ(table_size_4, table_.size());

  EXPECT_TRUE(table_.free(vec_acd));
  EXPECT_EQ(table_size_3, table_.size());

  EXPECT_TRUE(table_.free(vec_ac));
  EXPECT_EQ(table_size_2, table_.size());

  EXPECT_TRUE(table_.free(vec_ab));
  EXPECT_EQ(table_size_1, table_.size());

  EXPECT_TRUE(table_.free(vec_aa));
  EXPECT_EQ(table_size_1, table_.size());

  EXPECT_TRUE(table_.free(vec_a));
  EXPECT_EQ(table_size_0, table_.size());
}

} // namespace Stats
} // namespace Envoy
