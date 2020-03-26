#include <string>

#include "common/common/macros.h"
#include "common/common/mutex_tracer_impl.h"
#include "common/memory/stats.h"
#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/symbol_table_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/hash/hash_testing.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

// See comments in fake_symbol_table_impl.h: we need to test two implementations
// of SymbolTable, which we'll do with a test parameterized on this enum.
//
// Note that some of the tests cover behavior that is specific to the real
// SymbolTableImpl, and thus early-exit when the param is Fake.
//
// TODO(jmarantz): un-parameterize this test once SymbolTable is fully deployed
// and FakeSymbolTableImpl can be deleted.
enum class SymbolTableType {
  Real,
  Fake,
};

class StatNameTest : public testing::TestWithParam<SymbolTableType> {
protected:
  StatNameTest() {
    switch (GetParam()) {
    case SymbolTableType::Real: {
      auto table = std::make_unique<SymbolTableImpl>();
      real_symbol_table_ = table.get();
      table_ = std::move(table);
      break;
    }
    case SymbolTableType::Fake:
      auto table = std::make_unique<FakeSymbolTableImpl>();
      fake_symbol_table_ = table.get();
      table_ = std::move(table);
      break;
    }
    pool_ = std::make_unique<StatNamePool>(*table_);
  }

  ~StatNameTest() override { clearStorage(); }

  void clearStorage() {
    pool_->clear();
    EXPECT_EQ(0, table_->numSymbols());
  }

  SymbolVec getSymbols(StatName stat_name) {
    return SymbolTableImpl::Encoding::decodeSymbols(stat_name.data(), stat_name.dataSize());
  }
  Symbol monotonicCounter() { return real_symbol_table_->monotonicCounter(); }
  std::string encodeDecode(absl::string_view stat_name) {
    return table_->toString(makeStat(stat_name));
  }

  StatName makeStat(absl::string_view name) { return pool_->add(name); }

  std::vector<uint8_t> serializeDeserialize(uint64_t number) {
    return TestUtil::serializeDeserializeNumber(number);
  }

  FakeSymbolTableImpl* fake_symbol_table_{nullptr};
  SymbolTableImpl* real_symbol_table_{nullptr};
  std::unique_ptr<SymbolTable> table_;
  std::unique_ptr<StatNamePool> pool_;
};

INSTANTIATE_TEST_SUITE_P(StatNameTest, StatNameTest,
                         testing::ValuesIn({SymbolTableType::Real, SymbolTableType::Fake}));

TEST_P(StatNameTest, SerializeBytes) {
  EXPECT_EQ(std::vector<uint8_t>{1}, serializeDeserialize(1));
  EXPECT_EQ(std::vector<uint8_t>{127}, serializeDeserialize(127));
  EXPECT_EQ((std::vector<uint8_t>{128, 1}), serializeDeserialize(128));
  EXPECT_EQ((std::vector<uint8_t>{129, 1}), serializeDeserialize(129));
  EXPECT_EQ((std::vector<uint8_t>{255, 1}), serializeDeserialize(255));

  // This is the example from the image in stats.md.
  EXPECT_EQ((std::vector<uint8_t>{0x80 + 5, 2}), serializeDeserialize(261));

  EXPECT_EQ((std::vector<uint8_t>{255, 127}), serializeDeserialize(16383));
  EXPECT_EQ((std::vector<uint8_t>{128, 128, 1}), serializeDeserialize(16384));
  EXPECT_EQ((std::vector<uint8_t>{129, 128, 1}), serializeDeserialize(16385));

  auto power2 = [](uint32_t exp) -> uint64_t {
    uint64_t one = 1;
    return one << exp;
  };
  EXPECT_EQ((std::vector<uint8_t>{255, 255, 127}), serializeDeserialize(power2(21) - 1));
  EXPECT_EQ((std::vector<uint8_t>{128, 128, 128, 1}), serializeDeserialize(power2(21)));
  EXPECT_EQ((std::vector<uint8_t>{129, 128, 128, 1}), serializeDeserialize(power2(21) + 1));
  EXPECT_EQ((std::vector<uint8_t>{255, 255, 255, 127}), serializeDeserialize(power2(28) - 1));
  EXPECT_EQ((std::vector<uint8_t>{128, 128, 128, 128, 1}), serializeDeserialize(power2(28)));
  EXPECT_EQ((std::vector<uint8_t>{129, 128, 128, 128, 1}), serializeDeserialize(power2(28) + 1));
  EXPECT_EQ((std::vector<uint8_t>{255, 255, 255, 255, 127}), serializeDeserialize(power2(35) - 1));
  EXPECT_EQ((std::vector<uint8_t>{128, 128, 128, 128, 128, 1}), serializeDeserialize(power2(35)));
  EXPECT_EQ((std::vector<uint8_t>{129, 128, 128, 128, 128, 1}),
            serializeDeserialize(power2(35) + 1));

  for (uint32_t i = 0; i < 17000; ++i) {
    serializeDeserialize(i);
  }
}

TEST_P(StatNameTest, SerializeStrings) {
  TestUtil::serializeDeserializeString("");
  TestUtil::serializeDeserializeString("Hello, world!");
  TestUtil::serializeDeserializeString("embedded\0\nul");
  TestUtil::serializeDeserializeString(std::string(200, 'a'));
  TestUtil::serializeDeserializeString(std::string(2000, 'a'));
  TestUtil::serializeDeserializeString(std::string(20000, 'a'));
  TestUtil::serializeDeserializeString(std::string(200000, 'a'));
  TestUtil::serializeDeserializeString(std::string(2000000, 'a'));
  TestUtil::serializeDeserializeString(std::string(20000000, 'a'));
}

TEST_P(StatNameTest, AllocFree) { encodeDecode("hello.world"); }

TEST_P(StatNameTest, TestArbitrarySymbolRoundtrip) {
  const std::vector<std::string> stat_names = {"", " ", "  ", ",", "\t", "$", "%", "`", ".x"};
  for (auto& stat_name : stat_names) {
    EXPECT_EQ(stat_name, encodeDecode(stat_name));
  }
}

TEST_P(StatNameTest, TestEmpty) {
  EXPECT_TRUE(makeStat("").empty());
  EXPECT_FALSE(makeStat("x").empty());
  EXPECT_TRUE(StatName().empty());
}

TEST_P(StatNameTest, TestDynamic100k) {
  // Tests 100k different sizes of dynamic stat, covering all kinds of
  // corner cases of spilling over into multi-byte lengths.

  std::string stat_str("dynamic_stat.x");
  for (int i = 0; i < 100 * 1000; ++i) {
    char ch = i % 256;
    if (ch == '.') {
      ch = 'x';
    }
    stat_str += ch;
    StatNameDynamicStorage storage(stat_str, *table_);
    StatName dynamic = storage.statName();
    EXPECT_EQ(stat_str, table_->toString(dynamic));
    SymbolTable::StoragePtr joined = table_->join({makeStat("a.b"), dynamic, makeStat("c.d")});
    EXPECT_EQ(absl::StrCat("a.b.", stat_str, ".c.d"), table_->toString(StatName(joined.get())));
  }
}

TEST_P(StatNameTest, TestDynamicPools) {
  // Same test for a dynamically allocated name. The only difference between
  // the behavior with a remembered vs dynamic name is that when looking
  // up a remembered name, a mutex is not taken. But we have no easy way
  // to test for that. So we'll at least cover the code.
  StatNameDynamicPool d1(*table_);
  const StatName dynamic = d1.add("dynamic");
  EXPECT_EQ("dynamic", table_->toString(dynamic));

  // The nature of the StatNameDynamicPool is that there is no sharing (and also no locks).
  EXPECT_NE(dynamic.data(), d1.add("dynamic").data());

  // Make sure blanks are always the same.
  const StatName blank = d1.add("");
  EXPECT_EQ("", table_->toString(blank));
  EXPECT_NE(blank.data(), d1.add("").data());
  EXPECT_NE(blank.data(), d1.add("").data());
  EXPECT_NE(blank.data(), d1.add(absl::string_view()).data());

  // There's another corner case for the same "dynamic" name from a
  // different set. Here we will get a different StatName object
  // out of the second set, though it will share the same underlying
  // symbol-table symbol.
  StatNameDynamicPool d2(*table_);
  const StatName dynamic2 = d2.add("dynamic");
  EXPECT_EQ("dynamic", table_->toString(dynamic2));
  EXPECT_NE(dynamic2.data(), d2.add("dynamic").data()); // No storage sharing.
  EXPECT_NE(dynamic2.data(), dynamic.data());
}

TEST_P(StatNameTest, TestDynamicHash) {
  StatNameDynamicPool dynamic(*table_);
  const StatName d1 = dynamic.add("dynamic");
  const StatName d2 = dynamic.add("dynamic");
  EXPECT_EQ(d1, d2);
  EXPECT_EQ(d1.hash(), d2.hash());
}

TEST_P(StatNameTest, Test100KSymbolsRoundtrip) {
  for (int i = 0; i < 100 * 1000; ++i) {
    const std::string stat_name = absl::StrCat("symbol_", i);
    EXPECT_EQ(stat_name, encodeDecode(stat_name));
  }
}

TEST_P(StatNameTest, TwoHundredTwoLevel) {
  for (int i = 0; i < 200; ++i) {
    const std::string stat_name = absl::StrCat("symbol_", i);
    EXPECT_EQ(stat_name, encodeDecode(stat_name));
  }
  EXPECT_EQ("http.foo", encodeDecode("http.foo"));
}

TEST_P(StatNameTest, TestLongSymbolName) {
  std::string long_name(100000, 'a');
  EXPECT_EQ(long_name, encodeDecode(long_name));
}

TEST_P(StatNameTest, TestLongSequence) {
  std::string long_name("a");
  for (int i = 0; i < 100000; ++i) {
    absl::StrAppend(&long_name, ".a");
  }

  EXPECT_EQ(long_name, encodeDecode(long_name));
}

TEST_P(StatNameTest, TestUnusualDelimitersRoundtrip) {
  const std::vector<std::string> stat_names = {".x",   "..x",    "...x",    "foo",     "foo.x",
                                               ".foo", ".foo.x", ".foo..x", "..foo.x", "..foo..x"};
  for (auto& stat_name : stat_names) {
    EXPECT_EQ(stat_name, encodeDecode(stat_name));
  }
}

TEST_P(StatNameTest, TestSuccessfulDoubleLookup) {
  StatName stat_name_1(makeStat("foo.bar.baz"));
  StatName stat_name_2(makeStat("foo.bar.baz"));
  EXPECT_EQ(stat_name_1, stat_name_2);
}

TEST_P(StatNameTest, TestSuccessfulDecode) {
  std::string stat_name = "foo.bar.baz";
  StatName stat_name_1(makeStat(stat_name));
  StatName stat_name_2(makeStat(stat_name));
  EXPECT_EQ(table_->toString(stat_name_1), table_->toString(stat_name_2));
  EXPECT_EQ(table_->toString(stat_name_1), stat_name);
}

class StatNameDeathTest : public StatNameTest {
public:
  void decodeSymbolVec(const SymbolVec& symbol_vec) {
    Thread::LockGuard lock(real_symbol_table_->lock_);
    for (Symbol symbol : symbol_vec) {
      real_symbol_table_->fromSymbol(symbol);
    }
  }
};
INSTANTIATE_TEST_SUITE_P(StatNameDeathTest, StatNameDeathTest,
                         testing::ValuesIn({SymbolTableType::Real}));

TEST_P(StatNameDeathTest, TestBadDecodes) {
  if (GetParam() == SymbolTableType::Fake) {
    return;
  }

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

TEST_P(StatNameTest, TestDifferentStats) {
  StatName stat_name_1(makeStat("foo.bar"));
  StatName stat_name_2(makeStat("bar.foo"));
  EXPECT_NE(table_->toString(stat_name_1), table_->toString(stat_name_2));
  EXPECT_NE(stat_name_1, stat_name_2);
}

TEST_P(StatNameTest, TestSymbolConsistency) {
  if (GetParam() == SymbolTableType::Fake) {
    return;
  }
  StatName stat_name_1(makeStat("foo.bar"));
  StatName stat_name_2(makeStat("bar.foo"));
  // We expect the encoding of "foo" in one context to be the same as another.
  SymbolVec vec_1 = getSymbols(stat_name_1);
  SymbolVec vec_2 = getSymbols(stat_name_2);
  EXPECT_EQ(vec_1[0], vec_2[1]);
  EXPECT_EQ(vec_2[0], vec_1[1]);
}

TEST_P(StatNameTest, TestIgnoreTrailingDots) {
  EXPECT_EQ("foo.bar", encodeDecode("foo.bar."));
  EXPECT_EQ("foo.bar", encodeDecode("foo.bar..."));
  EXPECT_EQ("", encodeDecode("."));
  EXPECT_EQ("", encodeDecode(".."));
}

TEST_P(StatNameTest, TestSameValueOnPartialFree) {
  // This should hold true for components as well. Since "foo" persists even when "foo.bar" is
  // freed, we expect both instances of "foo" to have the same symbol.
  makeStat("foo");
  StatNameStorage stat_foobar_1("foo.bar", *table_);
  SymbolVec stat_foobar_1_symbols = getSymbols(stat_foobar_1.statName());
  stat_foobar_1.free(*table_);
  StatName stat_foobar_2(makeStat("foo.bar"));
  SymbolVec stat_foobar_2_symbols = getSymbols(stat_foobar_2);

  EXPECT_EQ(stat_foobar_1_symbols[0],
            stat_foobar_2_symbols[0]); // Both "foo" components have the same symbol,
  // And we have no expectation for the "bar" components, because of the free pool.
}

TEST_P(StatNameTest, FreePoolTest) {
  if (GetParam() == SymbolTableType::Fake) {
    return;
  }

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
    EXPECT_EQ(monotonicCounter(), 6);
    EXPECT_EQ(table_->numSymbols(), 5);
    clearStorage();
  }
  EXPECT_EQ(monotonicCounter(), 6);
  EXPECT_EQ(table_->numSymbols(), 0);

  // These are different strings being encoded, but they should recycle through the same symbols as
  // the stats above.
  makeStat("1b");
  makeStat("2b");
  makeStat("3b");
  makeStat("4b");
  makeStat("5b");
  EXPECT_EQ(monotonicCounter(), 6);
  EXPECT_EQ(table_->numSymbols(), 5);

  makeStat("6");
  EXPECT_EQ(monotonicCounter(), 7);
  EXPECT_EQ(table_->numSymbols(), 6);
}

TEST_P(StatNameTest, TestShrinkingExpectation) {
  // We expect that as we free stat names, the memory used to store those underlying symbols will
  // be freed.
  // ::size() is a public function, but should only be used for testing.
  size_t table_size_0 = table_->numSymbols();

  auto make_stat_storage = [this](absl::string_view name) -> StatNameStorage {
    return StatNameStorage(name, *table_);
  };

  StatNameStorage stat_a(make_stat_storage("a"));
  size_t table_size_1 = table_->numSymbols();

  StatNameStorage stat_aa(make_stat_storage("a.a"));
  EXPECT_EQ(table_size_1, table_->numSymbols());

  StatNameStorage stat_ab(make_stat_storage("a.b"));
  size_t table_size_2 = table_->numSymbols();

  StatNameStorage stat_ac(make_stat_storage("a.c"));
  size_t table_size_3 = table_->numSymbols();

  StatNameStorage stat_acd(make_stat_storage("a.c.d"));
  size_t table_size_4 = table_->numSymbols();

  StatNameStorage stat_ace(make_stat_storage("a.c.e"));
  size_t table_size_5 = table_->numSymbols();
  EXPECT_GE(table_size_5, table_size_4);

  stat_ace.free(*table_);
  EXPECT_EQ(table_size_4, table_->numSymbols());

  stat_acd.free(*table_);
  EXPECT_EQ(table_size_3, table_->numSymbols());

  stat_ac.free(*table_);
  EXPECT_EQ(table_size_2, table_->numSymbols());

  stat_ab.free(*table_);
  EXPECT_EQ(table_size_1, table_->numSymbols());

  stat_aa.free(*table_);
  EXPECT_EQ(table_size_1, table_->numSymbols());

  stat_a.free(*table_);
  EXPECT_EQ(table_size_0, table_->numSymbols());
}

// In the tests above we use the StatNameStorage abstraction which is not the
// most space-efficient strategy in all cases. To use memory more effectively
// you may want to store bytes in a larger structure. For example, you might
// want to allocate two different StatName objects in contiguous memory. The
// safety-net here in terms of leaks is that SymbolTable will assert-fail if
// you don't free all the StatNames you've allocated bytes for. StatNameList
// provides this capability.
TEST_P(StatNameTest, List) {
  StatName names[] = {makeStat("hello.world"), makeStat("goodbye.world")};
  StatNameList name_list;
  EXPECT_FALSE(name_list.populated());
  table_->populateList(names, ARRAY_SIZE(names), name_list);
  EXPECT_TRUE(name_list.populated());

  // First, decode only the first name.
  name_list.iterate([this](StatName stat_name) -> bool {
    EXPECT_EQ("hello.world", table_->toString(stat_name));
    return false;
  });

  // Decode all the names.
  std::vector<std::string> decoded_strings;
  name_list.iterate([this, &decoded_strings](StatName stat_name) -> bool {
    decoded_strings.push_back(table_->toString(stat_name));
    return true;
  });
  ASSERT_EQ(2, decoded_strings.size());
  EXPECT_EQ("hello.world", decoded_strings[0]);
  EXPECT_EQ("goodbye.world", decoded_strings[1]);
  name_list.clear(*table_);
  EXPECT_FALSE(name_list.populated());
}

TEST_P(StatNameTest, HashTable) {
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

TEST_P(StatNameTest, Sort) {
  StatNameVec names{makeStat("a.c"),   makeStat("a.b"), makeStat("d.e"),
                    makeStat("d.a.a"), makeStat("d.a"), makeStat("a.c")};
  const StatNameVec sorted_names{makeStat("a.b"), makeStat("a.c"),   makeStat("a.c"),
                                 makeStat("d.a"), makeStat("d.a.a"), makeStat("d.e")};
  EXPECT_NE(names, sorted_names);
  std::sort(names.begin(), names.end(), StatNameLessThan(*table_));
  EXPECT_EQ(names, sorted_names);
}

TEST_P(StatNameTest, Concat2) {
  SymbolTable::StoragePtr joined = table_->join({makeStat("a.b"), makeStat("c.d")});
  EXPECT_EQ("a.b.c.d", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, ConcatFirstEmpty) {
  SymbolTable::StoragePtr joined = table_->join({makeStat(""), makeStat("c.d")});
  EXPECT_EQ("c.d", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, ConcatSecondEmpty) {
  SymbolTable::StoragePtr joined = table_->join({makeStat("a.b"), makeStat("")});
  EXPECT_EQ("a.b", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, ConcatAllEmpty) {
  SymbolTable::StoragePtr joined = table_->join({makeStat(""), makeStat("")});
  EXPECT_EQ("", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, Join3) {
  SymbolTable::StoragePtr joined =
      table_->join({makeStat("a.b"), makeStat("c.d"), makeStat("e.f")});
  EXPECT_EQ("a.b.c.d.e.f", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, Join3FirstEmpty) {
  SymbolTable::StoragePtr joined = table_->join({makeStat(""), makeStat("c.d"), makeStat("e.f")});
  EXPECT_EQ("c.d.e.f", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, Join3SecondEmpty) {
  SymbolTable::StoragePtr joined = table_->join({makeStat("a.b"), makeStat(""), makeStat("e.f")});
  EXPECT_EQ("a.b.e.f", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, Join3ThirdEmpty) {
  SymbolTable::StoragePtr joined = table_->join({makeStat("a.b"), makeStat("c.d"), makeStat("")});
  EXPECT_EQ("a.b.c.d", table_->toString(StatName(joined.get())));
}

TEST_P(StatNameTest, JoinAllEmpty) {
  SymbolTable::StoragePtr joined = table_->join({makeStat(""), makeStat(""), makeStat("")});
  EXPECT_EQ("", table_->toString(StatName(joined.get())));
}

// Validates that we don't get tsan or other errors when concurrently creating
// a large number of stats.
TEST_P(StatNameTest, RacingSymbolCreation) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  MutexTracerImpl& mutex_tracer = MutexTracerImpl::getOrCreateTracer();

  // Make 100 threads, each of which will race to encode an overlapping set of
  // symbols, triggering corner-cases in SymbolTable::toSymbol.
  constexpr int num_threads = 100;
  std::vector<Thread::ThreadPtr> threads;
  threads.reserve(num_threads);
  ConditionalInitializer creation, access, wait;
  absl::BlockingCounter creates(num_threads), accesses(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    threads.push_back(
        thread_factory.createThread([this, i, &creation, &access, &wait, &creates, &accesses]() {
          // Rotate between 20 different symbols to try to get some
          // contention. Based on a logging print statement in
          // SymbolTable::toSymbol(), this appears to trigger creation-races,
          // even when compiled with optimization.
          std::string stat_name_string = absl::StrCat("symbol", i % 20);

          // Block each thread on waking up a common condition variable,
          // so we make it likely to race on creation.
          creation.wait();
          StatNameManagedStorage initial(stat_name_string, *table_);
          creates.DecrementCount();

          access.wait();
          StatNameManagedStorage second(stat_name_string, *table_);
          accesses.DecrementCount();

          wait.wait();
        }));
  }
  creation.setReady();
  creates.Wait();

  int64_t create_contentions = mutex_tracer.numContentions();
  ENVOY_LOG_MISC(info, "Number of contentions: {}", create_contentions);

  access.setReady();
  accesses.Wait();

  // In a perfect world, we could use reader-locks in the SymbolTable
  // implementation, and there should be zero additional contentions
  // after latching 'create_contentions' above. And we can definitely
  // have this world, but this slows down BM_CreateRace in
  // symbol_table_speed_test.cc, even on a 72-core machine.
  //
  // Thus it is better to avoid symbol-table contention by refactoring
  // all stat-creation code to symbolize all stat string elements at
  // construction, as composition does not require a lock.
  //
  // See this commit
  // https://github.com/envoyproxy/envoy/pull/5321/commits/ef712d0f5a11ff49831c1935e8a2ef8a0a935bc9
  // for a working reader-lock implementation, which would pass this EXPECT:
  //     EXPECT_EQ(create_contentions, mutex_tracer.numContentions());
  //
  // Note also that we cannot guarantee there *will* be contentions
  // as a machine or OS is free to run all threads serially.

  wait.setReady();
  for (auto& thread : threads) {
    thread->join();
  }
}

TEST_P(StatNameTest, MutexContentionOnExistingSymbols) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  MutexTracerImpl& mutex_tracer = MutexTracerImpl::getOrCreateTracer();

  // Make 100 threads, each of which will race to encode an overlapping set of
  // symbols, triggering corner-cases in SymbolTable::toSymbol.
  constexpr int num_threads = 100;
  std::vector<Thread::ThreadPtr> threads;
  threads.reserve(num_threads);
  ConditionalInitializer creation, access, wait;
  absl::BlockingCounter creates(num_threads), accesses(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    threads.push_back(
        thread_factory.createThread([this, i, &creation, &access, &wait, &creates, &accesses]() {
          // Rotate between 20 different symbols to try to get some
          // contention. Based on a logging print statement in
          // SymbolTable::toSymbol(), this appears to trigger creation-races,
          // even when compiled with optimization.
          std::string stat_name_string = absl::StrCat("symbol", i % 20);

          // Block each thread on waking up a common condition variable,
          // so we make it likely to race on creation.
          creation.wait();
          StatNameManagedStorage initial(stat_name_string, *table_);
          creates.DecrementCount();

          access.wait();
          StatNameManagedStorage second(stat_name_string, *table_);
          accesses.DecrementCount();

          wait.wait();
        }));
  }
  creation.setReady();
  creates.Wait();

  int64_t create_contentions = mutex_tracer.numContentions();
  ENVOY_LOG_MISC(info, "Number of contentions: {}", create_contentions);

  // But when we access the already-existing symbols, we guarantee that no
  // further mutex contentions occur.
  access.setReady();
  accesses.Wait();

  // In a perfect world, we could use reader-locks in the SymbolTable
  // implementation, and there should be zero additional contentions
  // after latching 'create_contentions' above. And we can definitely
  // have this world, but this slows down BM_CreateRace in
  // symbol_table_speed_test.cc, even on a 72-core machine.
  //
  // Thus it is better to avoid symbol-table contention by refactoring
  // all stat-creation code to symbolize all stat string elements at
  // construction, as composition does not require a lock.
  //
  // See this commit
  // https://github.com/envoyproxy/envoy/pull/5321/commits/ef712d0f5a11ff49831c1935e8a2ef8a0a935bc9
  // for a working reader-lock implementation, which would pass this EXPECT:
  //     EXPECT_EQ(create_contentions, mutex_tracer.numContentions());
  //
  // Note also that we cannot guarantee there *will* be contentions
  // as a machine or OS is free to run all threads serially.

  wait.setReady();
  for (auto& thread : threads) {
    thread->join();
  }
}

TEST_P(StatNameTest, SharedStatNameStorageSetInsertAndFind) {
  StatNameStorageSet set;
  const int iters = 10;
  for (int i = 0; i < iters; ++i) {
    std::string foo = absl::StrCat("foo", i);
    auto insertion = set.insert(StatNameStorage(foo, *table_));
    StatNameManagedStorage temp_foo(foo, *table_);
    auto found = set.find(temp_foo.statName());
    EXPECT_EQ(found->statName().data(), insertion.first->statName().data());
  }
  StatNameManagedStorage bar("bar", *table_);
  EXPECT_EQ(set.end(), set.find(bar.statName()));
  EXPECT_EQ(iters, set.size());
  set.free(*table_);
}

TEST_P(StatNameTest, StatNameSet) {
  StatNameSetPtr set(table_->makeSet("set"));

  // Test that we get a consistent StatName object from a remembered name.
  set->rememberBuiltin("remembered");
  const StatName fallback = set->add("fallback");
  const Stats::StatName remembered = set->getBuiltin("remembered", fallback);
  EXPECT_EQ("remembered", table_->toString(remembered));
  EXPECT_EQ(remembered.data(), set->getBuiltin("remembered", fallback).data());
  EXPECT_EQ(fallback.data(), set->getBuiltin("not_remembered", fallback).data());
}

TEST_P(StatNameTest, StorageCopy) {
  StatName a = pool_->add("stat.name");
  StatNameStorage b_storage(a, *table_);
  StatName b = b_storage.statName();
  EXPECT_EQ(a, b);
  EXPECT_NE(a.data(), b.data());
  b_storage.free(*table_);
}

TEST_P(StatNameTest, RecentLookups) {
  if (GetParam() == SymbolTableType::Fake) {
    // Touch these for coverage of fake symbol tables, but they'll have no effect.
    table_->clearRecentLookups();
    table_->setRecentLookupCapacity(0);
    return;
  }

  StatNameSetPtr set1(table_->makeSet("set1"));
  table_->setRecentLookupCapacity(10);
  StatNameSetPtr set2(table_->makeSet("set2"));
  StatNameDynamicPool d1(*table_);
  d1.add("dynamic.stat1");
  StatNameDynamicPool d2(*table_);
  d2.add("dynamic.stat2");
  encodeDecode("direct.stat");

  std::vector<std::string> accum;
  uint64_t total = table_->getRecentLookups([&accum](absl::string_view name, uint64_t count) {
    accum.emplace_back(absl::StrCat(count, ": ", name));
  });
  EXPECT_EQ(1, total); // Dynamic pool adds don't count as recent lookups.
  std::string recent_lookups_str = absl::StrJoin(accum, " ");

  EXPECT_EQ("1: direct.stat", recent_lookups_str); // No dynamic-pool lookups take locks.

  table_->clearRecentLookups();
  uint32_t num_calls = 0;
  EXPECT_EQ(0,
            table_->getRecentLookups([&num_calls](absl::string_view, uint64_t) { ++num_calls; }));
  EXPECT_EQ(0, num_calls);
}

TEST_P(StatNameTest, StatNameEmptyEquivalent) {
  StatName empty1;
  StatName empty2 = makeStat("");
  StatName non_empty = makeStat("a");
  EXPECT_EQ(empty1, empty2);
  EXPECT_EQ(empty1.hash(), empty2.hash());
  EXPECT_NE(empty1, non_empty);
  EXPECT_NE(empty2, non_empty);
  EXPECT_NE(empty1.hash(), non_empty.hash());
  EXPECT_NE(empty2.hash(), non_empty.hash());
}

TEST_P(StatNameTest, SupportsAbslHash) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      StatName(),
      makeStat(""),
      makeStat("hello.world"),
  }));
}

// Tests the memory savings realized from using symbol tables with 1k
// clusters. This test shows the memory drops from almost 8M to less than
// 2M. Note that only SymbolTableImpl is tested for memory consumption,
// and not FakeSymbolTableImpl.
TEST(SymbolTableTest, Memory) {
  // Tests a stat-name allocation strategy.
  auto test_memory_usage = [](std::function<void(absl::string_view)> fn) -> size_t {
    TestUtil::MemoryTest memory_test;
    TestUtil::forEachSampleStat(1000, fn);
    return memory_test.consumedBytes();
  };

  size_t string_mem_used, symbol_table_mem_used;
  {
    std::vector<std::string> names;
    auto record_stat = [&names](absl::string_view stat) { names.push_back(std::string(stat)); };
    string_mem_used = test_memory_usage(record_stat);
  }
  {
    SymbolTableImpl table;
    std::vector<StatNameStorage> names;
    auto record_stat = [&names, &table](absl::string_view stat) {
      names.emplace_back(StatNameStorage(stat, table));
    };
    symbol_table_mem_used = test_memory_usage(record_stat);
    for (StatNameStorage& name : names) {
      name.free(table);
    }
  }

  // Make sure we don't regress. Data as of 2019/05/29:
  //
  // string_mem_used:        6710912 (libc++), 7759488 (libstdc++).
  // symbol_table_mem_used:  1726056 (3.9x) -- does not seem to depend on STL sizes.
  EXPECT_MEMORY_LE(string_mem_used, 7759488);
  EXPECT_MEMORY_LE(symbol_table_mem_used, string_mem_used / 3);
  EXPECT_MEMORY_EQ(symbol_table_mem_used, 1726056);
}

} // namespace Stats
} // namespace Envoy
