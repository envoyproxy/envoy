#include <string>

#include "common/memory/stats.h"
#include "common/stats/symbol_table_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class StatNameTest : public testing::Test {
protected:
  ~StatNameTest() { clearStorage(); }

  void clearStorage() {
    for (auto& stat_name_storage : stat_name_storage_) {
      stat_name_storage.free(table_);
    }
    stat_name_storage_.clear();
    EXPECT_EQ(0, table_.numSymbols());
  }

  SymbolVec getSymbols(StatName stat_name) {
    return SymbolEncoding::decodeSymbols(stat_name.data(), stat_name.numBytes());
  }
  std::string decodeSymbolVec(const SymbolVec& symbol_vec) { return table_.decode(symbol_vec); }
  Symbol monotonicCounter() { return table_.monotonicCounter(); }
  std::string encodeDecode(absl::string_view stat_name) {
    return makeStat(stat_name).toString(table_);
  }

  StatNameStorage makeStatStorage(absl::string_view name) { return StatNameStorage(name, table_); }

  StatName makeStat(absl::string_view name) {
    stat_name_storage_.emplace_back(makeStatStorage(name));
    return stat_name_storage_.back().statName();
  }

  SymbolTable table_;

  std::vector<StatNameStorage> stat_name_storage_;
};

TEST_F(StatNameTest, AllocFree) { encodeDecode("hello.world"); }

TEST_F(StatNameTest, TestArbitrarySymbolRoundtrip) {
  const std::vector<std::string> stat_names = {"", " ", "  ", ",", "\t", "$", "%", "`", "."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, encodeDecode(stat_name));
  }
}

TEST_F(StatNameTest, TestMillionSymbolsRoundtrip) {
  for (int i = 0; i < 1 * 1000 * 1000; ++i) {
    const std::string stat_name = absl::StrCat("symbol_", i);
    EXPECT_EQ(stat_name, encodeDecode(stat_name));
  }
}

TEST_F(StatNameTest, TestUnusualDelimitersRoundtrip) {
  const std::vector<std::string> stat_names = {".",    "..",    "...",    "foo",    "foo.",
                                               ".foo", ".foo.", ".foo..", "..foo.", "..foo.."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, encodeDecode(stat_name));
  }
}

TEST_F(StatNameTest, TestSuccessfulDoubleLookup) {
  StatName stat_name_1(makeStat("foo.bar.baz"));
  StatName stat_name_2(makeStat("foo.bar.baz"));
  EXPECT_EQ(stat_name_1, stat_name_2);
}

TEST_F(StatNameTest, TestSuccessfulDecode) {
  std::string stat_name = "foo.bar.baz";
  StatName stat_name_1(makeStat(stat_name));
  StatName stat_name_2(makeStat(stat_name));
  EXPECT_EQ(stat_name_1.toString(table_), stat_name_2.toString(table_));
  EXPECT_EQ(stat_name_1.toString(table_), stat_name);
}

TEST_F(StatNameTest, TestBadDecodes) {
  {
    // If a symbol doesn't exist, decoding it should trigger an ASSERT() and crash.
    SymbolVec bad_symbol_vec = {1}; // symbol 0 is the empty symbol.
    EXPECT_DEATH(decodeSymbolVec(bad_symbol_vec), "");
  }

  {
    StatName stat_name_1 = makeStat("foo");
    SymbolVec vec_1 = getSymbols(stat_name_1);
    // Decoding a symbol vec that exists is perfectly normal...
    EXPECT_NO_THROW(decodeSymbolVec(vec_1));
    clearStorage();
    // But when the StatName is destroyed, its symbols are as well.
    EXPECT_DEATH(decodeSymbolVec(vec_1), "");
  }
}

TEST_F(StatNameTest, TestDifferentStats) {
  StatName stat_name_1(makeStat("foo.bar"));
  StatName stat_name_2(makeStat("bar.foo"));
  EXPECT_NE(stat_name_1.toString(table_), stat_name_2.toString(table_));
  EXPECT_NE(stat_name_1, stat_name_2);
}

TEST_F(StatNameTest, TestSymbolConsistency) {
  StatName stat_name_1(makeStat("foo.bar"));
  StatName stat_name_2(makeStat("bar.foo"));
  // We expect the encoding of "foo" in one context to be the same as another.
  SymbolVec vec_1 = getSymbols(stat_name_1);
  SymbolVec vec_2 = getSymbols(stat_name_2);
  EXPECT_EQ(vec_1[0], vec_2[1]);
  EXPECT_EQ(vec_2[0], vec_1[1]);
}

TEST_F(StatNameTest, TestSameValueOnPartialFree) {
  // This should hold true for components as well. Since "foo" persists even when "foo.bar" is
  // freed, we expect both instances of "foo" to have the same symbol.
  makeStat("foo");
  StatNameStorage stat_foobar_1(makeStatStorage("foo.bar"));
  SymbolVec stat_foobar_1_symbols = getSymbols(stat_foobar_1.statName());
  stat_foobar_1.free(table_);
  StatName stat_foobar_2(makeStat("foo.bar"));
  SymbolVec stat_foobar_2_symbols = getSymbols(stat_foobar_2);

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
    makeStat("1a");
    makeStat("2a");
    makeStat("3a");
    makeStat("4a");
    makeStat("5a");
    EXPECT_EQ(monotonicCounter(), 5);
    EXPECT_EQ(table_.numSymbols(), 5);
    clearStorage();
  }
  EXPECT_EQ(monotonicCounter(), 5);
  EXPECT_EQ(table_.numSymbols(), 0);

  // These are different strings being encoded, but they should recycle through the same symbols as
  // the stats above.
  makeStat("1b");
  makeStat("2b");
  makeStat("3b");
  makeStat("4b");
  makeStat("5b");
  EXPECT_EQ(monotonicCounter(), 5);
  EXPECT_EQ(table_.numSymbols(), 5);

  makeStat("6");
  EXPECT_EQ(monotonicCounter(), 6);
  EXPECT_EQ(table_.numSymbols(), 6);
}

TEST_F(StatNameTest, TestShrinkingExpectation) {
  // We expect that as we free stat names, the memory used to store those underlying symbols will
  // be freed.
  // ::size() is a public function, but should only be used for testing.
  size_t table_size_0 = table_.numSymbols();

  StatNameStorage stat_a(makeStatStorage("a"));
  size_t table_size_1 = table_.numSymbols();

  StatNameStorage stat_aa(makeStatStorage("a.a"));
  EXPECT_EQ(table_size_1, table_.numSymbols());

  StatNameStorage stat_ab(makeStatStorage("a.b"));
  size_t table_size_2 = table_.numSymbols();

  StatNameStorage stat_ac(makeStatStorage("a.c"));
  size_t table_size_3 = table_.numSymbols();

  StatNameStorage stat_acd(makeStatStorage("a.c.d"));
  size_t table_size_4 = table_.numSymbols();

  StatNameStorage stat_ace(makeStatStorage("a.c.e"));
  size_t table_size_5 = table_.numSymbols();
  EXPECT_GE(table_size_5, table_size_4);

  stat_ace.free(table_);
  EXPECT_EQ(table_size_4, table_.numSymbols());

  stat_acd.free(table_);
  EXPECT_EQ(table_size_3, table_.numSymbols());

  stat_ac.free(table_);
  EXPECT_EQ(table_size_2, table_.numSymbols());

  stat_ab.free(table_);
  EXPECT_EQ(table_size_1, table_.numSymbols());

  stat_aa.free(table_);
  EXPECT_EQ(table_size_1, table_.numSymbols());

  stat_a.free(table_);
  EXPECT_EQ(table_size_0, table_.numSymbols());
}

// In the tests above we use the StatNameStorage abstraction which is not the
// most space-efficient strategy in all cases. To use memory more effectively
// you may want to store bytes in a larger structure. For example, you might
// want to allocate two different StatName objects in contiguous memory. The
// safety-net here in terms of leaks is that SymbolTable will assert-fail if
// you don't free all the StatNames you've allocated bytes for.
TEST_F(StatNameTest, StoringWithoutStatNameStorage) {
  SymbolEncoding hello_encoding = table_.encode("hello.world");
  SymbolEncoding goodbye_encoding = table_.encode("goodbye.world");
  size_t size = hello_encoding.bytesRequired() + goodbye_encoding.bytesRequired();
  size_t goodbye_offset = hello_encoding.bytesRequired();
  std::unique_ptr<SymbolStorage> storage(new uint8_t[size]);
  hello_encoding.moveToStorage(storage.get());
  goodbye_encoding.moveToStorage(storage.get() + goodbye_offset);

  StatName hello(storage.get());
  StatName goodbye(storage.get() + goodbye_offset);

  EXPECT_EQ("hello.world", hello.toString(table_));
  EXPECT_EQ("goodbye.world", goodbye.toString(table_));

  // If we don't explicitly call free() on the the StatName objects the
  // SymbolTable will assert on destruction.
  table_.free(hello);
  table_.free(goodbye);
}

TEST_F(StatNameTest, HashTable) {
  StatName ac = makeStat("a.c");
  StatName ab = makeStat("a.b");
  StatName de = makeStat("d.e");
  StatName da = makeStat("d.a");

  StatNameHashMap<int> name_int_map;
  name_int_map[ac] = 1;
  name_int_map[ab] = 0;
  name_int_map[de] = 3;
  name_int_map[da] = 2;

  EXPECT_EQ(0, name_int_map[ab]);
  EXPECT_EQ(1, name_int_map[ac]);
  EXPECT_EQ(2, name_int_map[da]);
  EXPECT_EQ(3, name_int_map[de]);
}

TEST_F(StatNameTest, Sort) {
  std::vector<StatName> names{makeStat("a.c"),   makeStat("a.b"), makeStat("d.e"),
                              makeStat("d.a.a"), makeStat("d.a"), makeStat("a.c")};
  const std::vector<StatName> sorted_names{makeStat("a.b"), makeStat("a.c"),   makeStat("a.c"),
                                           makeStat("d.a"), makeStat("d.a.a"), makeStat("d.e")};
  EXPECT_NE(names, sorted_names);
  std::sort(names.begin(), names.end(), StatNameLessThan(table_));
  EXPECT_EQ(names, sorted_names);
}

TEST_F(StatNameTest, Concat2) {
  StatNameJoiner joiner(makeStat("a.b"), makeStat("c.d"));
  EXPECT_EQ("a.b.c.d", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, ConcatFirstEmpty) {
  StatNameJoiner joiner(makeStat(""), makeStat("c.d"));
  EXPECT_EQ("c.d", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, ConcatSecondEmpty) {
  StatNameJoiner joiner(makeStat("a.b"), makeStat(""));
  EXPECT_EQ("a.b", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, ConcatAllEmpty) {
  StatNameJoiner joiner(makeStat(""), makeStat(""));
  EXPECT_EQ("", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, Join3) {
  StatNameJoiner joiner({makeStat("a.b"), makeStat("c.d"), makeStat("e.f")});
  EXPECT_EQ("a.b.c.d.e.f", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, Join3FirstEmpty) {
  StatNameJoiner joiner({makeStat(""), makeStat("c.d"), makeStat("e.f")});
  EXPECT_EQ("c.d.e.f", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, Join3SecondEmpty) {
  StatNameJoiner joiner({makeStat("a.b"), makeStat(""), makeStat("e.f")});
  EXPECT_EQ("a.b.e.f", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, Join3ThirdEmpty) {
  StatNameJoiner joiner({makeStat("a.b"), makeStat("c.d"), makeStat("")});
  EXPECT_EQ("a.b.c.d", joiner.statName().toString(table_));
}

TEST_F(StatNameTest, JoinAllEmpty) {
  StatNameJoiner joiner({makeStat(""), makeStat(""), makeStat("")});
  EXPECT_EQ("", joiner.statName().toString(table_));
}

// Tests the memory savings realized from using symbol tables with 1k clusters. This
// test shows the memory drops from almost 8M to less than 2M.
TEST(SymbolTableTest, Memory) {
  if (!TestUtil::hasDeterministicMallocStats()) {
    return;
  }

  // Tests a stat-name allocation strategy.
  auto test_memory_usage = [](std::function<void(absl::string_view)> fn) -> size_t {
    const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();
    TestUtil::forEachSampleStat(1000, fn);
    const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
    if (end_mem != 0) { // See warning below for asan, tsan, and mac.
      EXPECT_GT(end_mem, start_mem);
    }
    return end_mem - start_mem;
  };

  size_t string_mem_used, symbol_table_mem_used;
  {
    std::vector<std::string> names;
    auto record_stat = [&names](absl::string_view stat) { names.push_back(std::string(stat)); };
    string_mem_used = test_memory_usage(record_stat);
  }
  {
    SymbolTable table;
    std::vector<StatNameStorage> names;
    auto record_stat = [&names, &table](absl::string_view stat) {
      names.emplace_back(StatNameStorage(stat, table));
    };
    symbol_table_mem_used = test_memory_usage(record_stat);
    for (StatNameStorage& name : names) {
      name.free(table);
    }
  }

  // This test only works if Memory::Stats::totalCurrentlyAllocated() works, which
  // appears not to be the case in some tests, including asan, tsan, and mac.
  if (Memory::Stats::totalCurrentlyAllocated() == 0) {
    std::cerr << "SymbolTableTest.Memory comparison skipped due to malloc-stats returning 0."
              << std::endl;
  } else {
    // In manual tests, string memory used 7759488 in this example, and
    // symbol-table mem used 1739672. Setting the benchmark at 7759488/4 =
    // 1939872, which should allow for some slop and platform dependence
    // in the allocation library.

    EXPECT_LT(symbol_table_mem_used, string_mem_used / 4);
  }
}

} // namespace Stats
} // namespace Envoy
