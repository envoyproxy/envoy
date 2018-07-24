#include <string>

#include "common/stats/symbol_table_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class StatNameTest : public testing::Test {
public:
  StatNameTest() {}
  SymbolTableImpl table_;
};

TEST_F(StatNameTest, TestArbitrarySymbolRoundtrip) {
  std::vector<std::string> stat_names = {"", " ", "  ", ",", "\t", "$", "%", "`", "."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, table_.encode(stat_name)->toString());
  }
}

TEST_F(StatNameTest, TestUnusualDelimitersRoundtrip) {
  std::vector<std::string> stat_names = {".",    "..",    "...",    "foo",    "foo.",
                                         ".foo", ".foo.", ".foo..", "..foo.", "..foo.."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, table_.encode(stat_name)->toString());
  }
}

TEST_F(StatNameTest, TestSuccessfulDoubleLookup) {
  auto stat_name_1 = table_.encode("foo.bar.baz");
  auto stat_name_2 = table_.encode("foo.bar.baz");
  EXPECT_EQ(stat_name_1->toSymbols(), stat_name_2->toSymbols());
}

TEST_F(StatNameTest, TestSuccessfulDecode) {
  std::string stat_name = "foo.bar.baz";
  auto stat_name_1 = table_.encode(stat_name);
  auto stat_name_2 = table_.encode(stat_name);
  EXPECT_EQ(stat_name_1->toString(), stat_name_2->toString());
  EXPECT_EQ(stat_name_1->toString(), stat_name);
}

TEST_F(StatNameTest, TestDifferentStats) {
  auto stat_name_1 = table_.encode("foo.bar");
  auto stat_name_2 = table_.encode("bar.foo");
  EXPECT_NE(stat_name_1->toSymbols(), stat_name_2->toSymbols());
  EXPECT_NE(stat_name_1->toString(), stat_name_2->toString());
}

TEST_F(StatNameTest, TestSymbolConsistency) {
  auto stat_name_1 = table_.encode("foo.bar");
  auto stat_name_2 = table_.encode("bar.foo");
  // We expect the encoding of "foo" in one context to be the same as another.
  EXPECT_EQ(stat_name_1->toSymbols()[0], stat_name_2->toSymbols()[1]);
  EXPECT_EQ(stat_name_2->toSymbols()[0], stat_name_1->toSymbols()[1]);
}

// We also wish to test the internals of what gets called inside a SymbolTable, even though these
// functions are not user-facing.
class SymbolTableTest : public testing::Test {
public:
  SymbolTableTest() {}
  SymbolTableImpl table_;
};

// TODO(ambuc): Test decoding an invalid symbol vector. This will probably need a test which
// implements a mock StatNameImpl, so that it can get access to .decode(), which is protected.

// Even though the symbol table does manual reference counting, curr_counter_ is monotonically
// increasing. So encoding "foo", freeing the sole stat containing "foo", and then re-encoding
// "foo" will produce a different symbol each time.
TEST_F(SymbolTableTest, TestNewValueAfterFree) {
  {
    StatNamePtr stat_name_1 = table_.encode("foo");
    SymbolVec stat_name_1_symbols = stat_name_1->toSymbols();
    stat_name_1.reset();
    StatNamePtr stat_name_2 = table_.encode("foo");
    SymbolVec stat_name_2_symbols = stat_name_2->toSymbols();
    EXPECT_NE(stat_name_1_symbols, stat_name_2_symbols);
  }

  {
    // This should hold true for components as well. Since "foo" persists even when "foo.bar" is
    // freed, we expect both instances of "foo" to have the same symbol, but each instance of
    // "bar" to have a different symbol.
    StatNamePtr stat_foo = table_.encode("foo");
    StatNamePtr stat_foobar_1 = table_.encode("foo.bar");
    SymbolVec stat_foobar_1_symbols = stat_foobar_1->toSymbols();
    stat_foobar_1.reset();

    StatNamePtr stat_foobar_2 = table_.encode("foo.bar");
    SymbolVec stat_foobar_2_symbols = stat_foobar_2->toSymbols();

    EXPECT_EQ(stat_foobar_1_symbols[0],
              stat_foobar_2_symbols[0]); // Both "foo" components have the same symbol,
    EXPECT_NE(stat_foobar_1_symbols[1],
              stat_foobar_2_symbols[1]); // but the two "bar" components do not.
  }
}

TEST_F(SymbolTableTest, TestShrinkingExpectation) {
  // We expect that as we free stat names, the memory used to store those underlying symbols will
  // be freed.
  // ::size() is a public function, but should only be used for testing.
  size_t table_size_0 = table_.size();

  StatNamePtr stat_a = table_.encode("a");
  size_t table_size_1 = table_.size();

  StatNamePtr stat_aa = table_.encode("a.a");
  EXPECT_EQ(table_size_1, table_.size());

  StatNamePtr stat_ab = table_.encode("a.b");
  size_t table_size_2 = table_.size();

  StatNamePtr stat_ac = table_.encode("a.c");
  size_t table_size_3 = table_.size();

  StatNamePtr stat_acd = table_.encode("a.c.d");
  size_t table_size_4 = table_.size();

  StatNamePtr stat_ace = table_.encode("a.c.e");
  size_t table_size_5 = table_.size();
  EXPECT_GE(table_size_5, table_size_4);

  stat_ace.reset();
  EXPECT_EQ(table_size_4, table_.size());

  stat_acd.reset();
  EXPECT_EQ(table_size_3, table_.size());

  stat_ac.reset();
  EXPECT_EQ(table_size_2, table_.size());

  stat_ab.reset();
  EXPECT_EQ(table_size_1, table_.size());

  stat_aa.reset();
  EXPECT_EQ(table_size_1, table_.size());

  stat_a.reset();
  EXPECT_EQ(table_size_0, table_.size());
}

} // namespace Stats
} // namespace Envoy
