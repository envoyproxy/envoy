#include <string>

#include "common/stats/symbol_table_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class StatNameTest : public testing::Test {
protected:
  SymbolVec getSymbols(StatName* stat_name_ptr) {
    StatNameImpl& impl = dynamic_cast<StatNameImpl&>(*stat_name_ptr);
    return impl.symbolVec();
  }
  std::string decodeSymbolVec(const SymbolVec& symbol_vec) { return table_.decode(symbol_vec); }
  Symbol monotonicCounter() { return table_.monotonicCounter(); }

  SymbolTableImpl table_;
};

TEST_F(StatNameTest, TestArbitrarySymbolRoundtrip) {
  const std::vector<std::string> stat_names = {"", " ", "  ", ",", "\t", "$", "%", "`", "."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, table_.encode(stat_name)->toString());
  }
}

TEST_F(StatNameTest, TestUnusualDelimitersRoundtrip) {
  const std::vector<std::string> stat_names = {".",    "..",    "...",    "foo",    "foo.",
                                               ".foo", ".foo.", ".foo..", "..foo.", "..foo.."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, table_.encode(stat_name)->toString());
  }
}

TEST_F(StatNameTest, TestSuccessfulDoubleLookup) {
  StatNamePtr stat_name_1 = table_.encode("foo.bar.baz");
  StatNamePtr stat_name_2 = table_.encode("foo.bar.baz");
  EXPECT_EQ(getSymbols(stat_name_1.get()), getSymbols(stat_name_2.get()));
}

TEST_F(StatNameTest, TestSuccessfulDecode) {
  std::string stat_name = "foo.bar.baz";
  auto stat_name_1 = table_.encode(stat_name);
  auto stat_name_2 = table_.encode(stat_name);
  EXPECT_EQ(stat_name_1->toString(), stat_name_2->toString());
  EXPECT_EQ(stat_name_1->toString(), stat_name);
}

TEST_F(StatNameTest, TestBadDecodes) {
  {
    // If a symbol doesn't exist, decoding it should trigger an ASSERT() and crash.
    SymbolVec bad_symbol_vec = {0};
    EXPECT_DEATH(decodeSymbolVec(bad_symbol_vec), "");
  }

  {
    StatNamePtr stat_name_1 = table_.encode("foo");
    SymbolVec vec_1 = getSymbols(stat_name_1.get());
    // Decoding a symbol vec that exists is perfectly normal...
    EXPECT_NO_THROW(decodeSymbolVec(vec_1));
    stat_name_1.reset();
    // But when the StatNamePtr is destroyed, its symbols are as well.
    EXPECT_DEATH(decodeSymbolVec(vec_1), "");
  }
}

TEST_F(StatNameTest, TestDifferentStats) {
  auto stat_name_1 = table_.encode("foo.bar");
  auto stat_name_2 = table_.encode("bar.foo");
  EXPECT_NE(stat_name_1->toString(), stat_name_2->toString());
  EXPECT_NE(getSymbols(stat_name_1.get()), getSymbols(stat_name_2.get()));
}

TEST_F(StatNameTest, TestSymbolConsistency) {
  auto stat_name_1 = table_.encode("foo.bar");
  auto stat_name_2 = table_.encode("bar.foo");
  // We expect the encoding of "foo" in one context to be the same as another.
  SymbolVec vec_1 = getSymbols(stat_name_1.get());
  SymbolVec vec_2 = getSymbols(stat_name_2.get());
  EXPECT_EQ(vec_1[0], vec_2[1]);
  EXPECT_EQ(vec_2[0], vec_1[1]);
}

TEST_F(StatNameTest, TestSameValueOnPartialFree) {
  // This should hold true for components as well. Since "foo" persists even when "foo.bar" is
  // freed, we expect both instances of "foo" to have the same symbol.
  StatNamePtr stat_foo = table_.encode("foo");
  StatNamePtr stat_foobar_1 = table_.encode("foo.bar");
  SymbolVec stat_foobar_1_symbols = getSymbols(stat_foobar_1.get());
  stat_foobar_1.reset();

  StatNamePtr stat_foobar_2 = table_.encode("foo.bar");
  SymbolVec stat_foobar_2_symbols = getSymbols(stat_foobar_2.get());

  EXPECT_EQ(stat_foobar_1_symbols[0],
            stat_foobar_2_symbols[0]); // Both "foo" components have the same symbol,
  // And we have no expectation for the "bar" components, because of the free pool.
}

TEST_F(StatNameTest, FreePoolTest) {
  // To ensure that the free pool is being used, we should be able to cycle through a large number
  // of stats while validating that:
  //   a) the size of the table has not increased, and
  //   b) the monotonically increasing counter has not risen to more than the maximum number of
  //   coexisting symbols during the life of the table.

  {
    StatNamePtr stat_1 = table_.encode("1a");
    StatNamePtr stat_2 = table_.encode("2a");
    StatNamePtr stat_3 = table_.encode("3a");
    StatNamePtr stat_4 = table_.encode("4a");
    StatNamePtr stat_5 = table_.encode("5a");
    EXPECT_EQ(monotonicCounter(), 5);
    EXPECT_EQ(table_.size(), 5);
  }
  EXPECT_EQ(monotonicCounter(), 5);
  EXPECT_EQ(table_.size(), 0);

  // These are different strings being encoded, but they should recycle through the same symbols as
  // the stats above.
  StatNamePtr stat_1 = table_.encode("1b");
  StatNamePtr stat_2 = table_.encode("2b");
  StatNamePtr stat_3 = table_.encode("3b");
  StatNamePtr stat_4 = table_.encode("4b");
  StatNamePtr stat_5 = table_.encode("5b");
  EXPECT_EQ(monotonicCounter(), 5);
  EXPECT_EQ(table_.size(), 5);

  StatNamePtr stat_6 = table_.encode("6");
  EXPECT_EQ(monotonicCounter(), 6);
  EXPECT_EQ(table_.size(), 6);
}

TEST_F(StatNameTest, TestShrinkingExpectation) {
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
