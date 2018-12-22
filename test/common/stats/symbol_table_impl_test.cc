#include <string>

#include "common/common/mutex_tracer_impl.h"
#include "common/memory/stats.h"
#include "common/stats/fake_symbol_table.h"
#include "common/stats/symbol_table_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

template<typename SymbolTableClass> class StatNameTest : public testing::Test {
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
    return SymbolEncoding::decodeSymbols(stat_name.data(), stat_name.dataSize());
  }
  std::string decodeSymbolVec(const SymbolVec& symbol_vec) {
    return table_.decodeSymbolVec(symbol_vec);
  }
  Symbol monotonicCounter() { return table_.monotonicCounter(); }
  std::string encodeDecode(absl::string_view stat_name) {
    return table_.toString(makeStat(stat_name));
  }

  StatNameStorage makeStatStorage(absl::string_view name) { return StatNameStorage(name, table_); }

  StatName makeStat(absl::string_view name) {
    stat_name_storage_.emplace_back(makeStatStorage(name));
    return stat_name_storage_.back().statName();
  }

  SymbolTableClass table_;

  std::vector<StatNameStorage> stat_name_storage_;
};

using SymbolTableImplementations = ::testing::Types<SymbolTable, FakeSymbolTable>;
TYPED_TEST_CASE(StatNameTest, SymbolTableImplementations);

TYPED_TEST(StatNameTest, AllocFree) { this->encodeDecode("hello.world"); }
TYPED_TEST(StatNameTest, TestArbitrarySymbolRoundtrip) {
  const std::vector<std::string> stat_names = {"", " ", "  ", ",", "\t", "$", "%", "`", "."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, this->encodeDecode(stat_name));
  }
}

TEST_F(StatNameTest, Test100kSymbolsRoundtrip) {
  for (int i = 0; i < 100 * 1000; ++i) {
    const std::string stat_name = absl::StrCat("symbol_", i);
    EXPECT_EQ(stat_name, this->encodeDecode(stat_name));
  }
}

TEST_F(StatNameTest, TestUnusualDelimitersRoundtrip) {
  const std::vector<std::string> stat_names = {".",    "..",    "...",    "foo",    "foo.",
                                               ".foo", ".foo.", ".foo..", "..foo.", "..foo.."};
  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, this->encodeDecode(stat_name));
  }
}

TEST_F(StatNameTest, TestSuccessfulDoubleLookup) {
  StatName stat_name_1(this->makeStat("foo.bar.baz"));
  StatName stat_name_2(this->makeStat("foo.bar.baz"));
  EXPECT_EQ(stat_name_1, stat_name_2);
}

TEST_F(StatNameTest, TestSuccessfulDecode) {
  std::string stat_name = "foo.bar.baz";
  StatName stat_name_1(this->makeStat(stat_name));
  StatName stat_name_2(this->makeStat(stat_name));
  EXPECT_EQ(this->table_.toString(stat_name_1), this->table_.toString(stat_name_2));
  EXPECT_EQ(this->table_.toString(stat_name_1), stat_name);
}

TEST_F(StatNameTest, TestBadDecodes) {
  {
    // If a symbol doesn't exist, decoding it should trigger an ASSERT() and crash.
    SymbolVec bad_symbol_vec = {1}; // symbol 0 is the empty symbol.
    EXPECT_DEATH(this->decodeSymbolVec(bad_symbol_vec), "");
  }

  {
    StatName stat_name_1 = this->makeStat("foo");
    SymbolVec vec_1 = this->getSymbols(stat_name_1);
    // Decoding a symbol vec that exists is perfectly normal...
    EXPECT_NO_THROW(this->decodeSymbolVec(vec_1));
    this->clearStorage();
    // But when the StatName is destroyed, its symbols are as well.
    EXPECT_DEATH(this->decodeSymbolVec(vec_1), "");
  }
}

TEST_F(StatNameTest, TestDifferentStats) {
  StatName stat_name_1(this->makeStat("foo.bar"));
  StatName stat_name_2(this->makeStat("bar.foo"));
  EXPECT_NE(this->table_.toString(stat_name_1), this->table_.toString(stat_name_2));
  EXPECT_NE(stat_name_1, stat_name_2);
}

TEST_F(StatNameTest, TestSymbolConsistency) {
  StatName stat_name_1(this->makeStat("foo.bar"));
  StatName stat_name_2(this->makeStat("bar.foo"));
  // We expect the encoding of "foo" in one context to be the same as another.
  SymbolVec vec_1 = this->getSymbols(stat_name_1);
  SymbolVec vec_2 = this->getSymbols(stat_name_2);
  EXPECT_EQ(vec_1[0], vec_2[1]);
  EXPECT_EQ(vec_2[0], vec_1[1]);
}

TEST_F(StatNameTest, TestSameValueOnPartialFree) {
  // This should hold true for components as well. Since "foo" persists even when "foo.bar" is
  // freed, we expect both instances of "foo" to have the same symbol.
  this->makeStat("foo");
  StatNameStorage stat_foobar_1(this->makeStatStorage("foo.bar"));
  SymbolVec stat_foobar_1_symbols = this->getSymbols(stat_foobar_1.statName());
  stat_foobar_1.free(this->table_);
  StatName stat_foobar_2(this->makeStat("foo.bar"));
  SymbolVec stat_foobar_2_symbols = this->getSymbols(stat_foobar_2);

  EXPECT_EQ(stat_foobar_1_symbols[0],
            stat_foobar_2_symbols[0]); // Both "foo" components have the same symbol,
  // And we have no expectation for the "bar" components, because of the free pool.
}

using SymbolTableStatNameTest = StatNameTest<SymbolTable>;

TEST_F(SymbolTableStatNameTest, FreePoolTest) {
  // To ensure that the free pool is being used, we should be able to cycle through a large number
  // of stats while validating that:
  //   a) the size of the table has not increased, and
  //   b) the monotonically increasing counter has not risen to more than the maximum number of
  //   coexisting symbols during the life of the table.

  {
    this->makeStat("1a");
    this->makeStat("2a");
    this->makeStat("3a");
    this->makeStat("4a");
    this->makeStat("5a");
    EXPECT_EQ(this->monotonicCounter(), 5);
    EXPECT_EQ(this->table_.numSymbols(), 5);
    clearStorage();
  }
  EXPECT_EQ(this->monotonicCounter(), 5);
  EXPECT_EQ(this->table_.numSymbols(), 0);

  // These are different strings being encoded, but they should recycle through the same symbols as
  // the stats above.
  this->makeStat("1b");
  this->makeStat("2b");
  this->makeStat("3b");
  this->makeStat("4b");
  this->makeStat("5b");
  EXPECT_EQ(this->monotonicCounter(), 5);
  EXPECT_EQ(this->table_.numSymbols(), 5);

  this->makeStat("6");
  EXPECT_EQ(monotonicCounter(), 6);
  EXPECT_EQ(this->table_.numSymbols(), 6);
}

TEST_F(StatNameTest, TestShrinkingExpectation) {
  // We expect that as we free stat names, the memory used to store those underlying symbols will
  // be freed.
  // ::size() is a public function, but should only be used for testing.
  size_t table_size_0 = this->table_.numSymbols();

  StatNameStorage stat_a(this->makeStatStorage("a"));
  size_t table_size_1 = this->table_.numSymbols();

  StatNameStorage stat_aa(this->makeStatStorage("a.a"));
  EXPECT_EQ(table_size_1, this->table_.numSymbols());

  StatNameStorage stat_ab(this->makeStatStorage("a.b"));
  size_t table_size_2 = this->table_.numSymbols();

  StatNameStorage stat_ac(this->makeStatStorage("a.c"));
  size_t table_size_3 = this->table_.numSymbols();

  StatNameStorage stat_acd(this->makeStatStorage("a.c.d"));
  size_t table_size_4 = this->table_.numSymbols();

  StatNameStorage stat_ace(this->makeStatStorage("a.c.e"));
  size_t table_size_5 = this->table_.numSymbols();
  EXPECT_GE(table_size_5, table_size_4);

  stat_ace.free(this->table_);
  EXPECT_EQ(table_size_4, this->table_.numSymbols());

  stat_acd.free(this->table_);
  EXPECT_EQ(table_size_3, this->table_.numSymbols());

  stat_ac.free(this->table_);
  EXPECT_EQ(table_size_2, this->table_.numSymbols());

  stat_ab.free(this->table_);
  EXPECT_EQ(table_size_1, this->table_.numSymbols());

  stat_aa.free(this->table_);
  EXPECT_EQ(table_size_1, this->table_.numSymbols());

  stat_a.free(this->table_);
  EXPECT_EQ(table_size_0, this->table_.numSymbols());
}

// In the tests above we use the StatNameStorage abstraction which is not the
// most space-efficient strategy in all cases. To use memory more effectively
// you may want to store bytes in a larger structure. For example, you might
// want to allocate two different StatName objects in contiguous memory. The
// safety-net here in terms of leaks is that SymbolTable will assert-fail if
// you don't free all the StatNames you've allocated bytes for.
TEST_F(StatNameTest, StoringWithoutStatNameStorage) {
  SymbolEncoding hello_encoding = this->table_.encode("hello.world");
  SymbolEncoding goodbye_encoding = this->table_.encode("goodbye.world");
  size_t size = hello_encoding.bytesRequired() + goodbye_encoding.bytesRequired();
  size_t goodbye_offset = hello_encoding.bytesRequired();
  std::unique_ptr<SymbolStorage> storage(new uint8_t[size]);
  hello_encoding.moveToStorage(storage.get());
  goodbye_encoding.moveToStorage(storage.get() + goodbye_offset);

  StatName hello(storage.get());
  StatName goodbye(storage.get() + goodbye_offset);

  EXPECT_EQ("hello.world", this->table_.toString(hello));
  EXPECT_EQ("goodbye.world", this->table_.toString(goodbye));

  // If we don't explicitly call free() on the the StatName objects the
  // SymbolTable will assert on destruction.
  this->table_.free(hello);
  this->table_.free(goodbye);
}

TEST_F(StatNameTest, HashTable) {
  StatName ac = this->makeStat("a.c");
  StatName ab = this->makeStat("a.b");
  StatName de = this->makeStat("d.e");
  StatName da = this->makeStat("d.a");

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
  std::vector<StatName> names{this->makeStat("a.c"),   this->makeStat("a.b"), this->makeStat("d.e"),
                              this->makeStat("d.a.a"), this->makeStat("d.a"), this->makeStat("a.c")};
  const std::vector<StatName> sorted_names{this->makeStat("a.b"), this->makeStat("a.c"),   this->makeStat("a.c"),
                                           this->makeStat("d.a"), this->makeStat("d.a.a"), this->makeStat("d.e")};
  EXPECT_NE(names, sorted_names);
  std::sort(names.begin(), names.end(), StatNameLessThan(this->table_));
  EXPECT_EQ(names, sorted_names);
}

TEST_F(StatNameTest, Concat2) {
  StatNameJoiner joiner(this->makeStat("a.b"), this->makeStat("c.d"));
  EXPECT_EQ("a.b.c.d", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, ConcatFirstEmpty) {
  StatNameJoiner joiner(this->makeStat(""), this->makeStat("c.d"));
  EXPECT_EQ("c.d", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, ConcatSecondEmpty) {
  StatNameJoiner joiner(this->makeStat("a.b"), this->makeStat(""));
  EXPECT_EQ("a.b", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, ConcatAllEmpty) {
  StatNameJoiner joiner(this->makeStat(""), this->makeStat(""));
  EXPECT_EQ("", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, Join3) {
  StatNameJoiner joiner({this->makeStat("a.b"), this->makeStat("c.d"), this->makeStat("e.f")});
  EXPECT_EQ("a.b.c.d.e.f", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, Join3FirstEmpty) {
  StatNameJoiner joiner({this->makeStat(""), this->makeStat("c.d"), this->makeStat("e.f")});
  EXPECT_EQ("c.d.e.f", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, Join3SecondEmpty) {
  StatNameJoiner joiner({this->makeStat("a.b"), this->makeStat(""), this->makeStat("e.f")});
  EXPECT_EQ("a.b.e.f", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, Join3ThirdEmpty) {
  StatNameJoiner joiner({this->makeStat("a.b"), this->makeStat("c.d"), this->makeStat("")});
  EXPECT_EQ("a.b.c.d", this->table_.toString(joiner.statName()));
}

TEST_F(StatNameTest, JoinAllEmpty) {
  StatNameJoiner joiner({this->makeStat(""), this->makeStat(""), this->makeStat("")});
  EXPECT_EQ("", this->table_.toString(joiner.statName()));
}

namespace {

// This class functions like absl::Notification except the usage of SignalAll()
// appears to trigger tighter simultaneous wakeups in multiple threads. Note
// that the synchronization mechanism in
//     https://github.com/abseil/abseil-cpp/blob/master/absl/synchronization/notification.h
// has timing properties that do not seem to trigger the race condition in
// SymbolTable::toSymbol() where the find() under read-lock fails, but by the
// time the insert() under write-lock occurs, the symbol has been added by
// another thread.
class Notifier {
public:
  Notifier() : cond_(false) {}

  void notify() {
    absl::MutexLock lock(&mutex_);
    cond_ = true;
    cond_var_.SignalAll();
  }

  void wait() {
    absl::MutexLock lock(&mutex_);
    while (!cond_) {
      cond_var_.Wait(&mutex_);
    }
  }

private:
  absl::Mutex mutex_;
  bool cond_ GUARDED_BY(mutex_);
  absl::CondVar cond_var_;
};

} // namespace

TEST_F(StatNameTest, RacingSymbolCreation) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  MutexTracerImpl& mutex_tracer = MutexTracerImpl::getOrCreateTracer();

  // Make 100 threads, each of which will race to encode an overlapping set of
  // symbols, triggering corner-cases in SymbolTable::toSymbol.
  constexpr int num_threads = 100;
  std::vector<Thread::ThreadPtr> threads;
  threads.reserve(num_threads);
  Notifier creation, access, wait;
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
          StatNameTempStorage initial(stat_name_string, this->table_);
          creates.DecrementCount();

          access.wait();
          StatNameTempStorage second(stat_name_string, this->table_);
          accesses.DecrementCount();

          wait.wait();
        }));
  }
  creation.notify();
  creates.Wait();

  int64_t create_contentions = mutex_tracer.numContentions();
  std::cerr << "Number of contentions: " << create_contentions << std::endl;

  // But when we access the already-existing symbols, we guarantee that no
  // further mutex contentions occur.
  access.notify();
  accesses.Wait();
  EXPECT_EQ(create_contentions, mutex_tracer.numContentions());

  wait.notify();
  for (auto& thread : threads) {
    thread->join();
  }
}

TEST_F(StatNameTest, MutexContentionOnExistingSymbols) {
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
          StatNameTempStorage initial(stat_name_string, table_);
          creates.DecrementCount();

          access.wait();
          StatNameTempStorage second(stat_name_string, table_);
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
    ENVOY_LOG_MISC(info,
                   "SymbolTableTest.Memory comparison skipped due to malloc-stats returning 0.");
  } else {
    EXPECT_LT(symbol_table_mem_used, string_mem_used / 4);
    EXPECT_LT(symbol_table_mem_used, 1750000); // Dec 16, 2018: 1744280 bytes.
  }
}

} // namespace Stats
} // namespace Envoy
