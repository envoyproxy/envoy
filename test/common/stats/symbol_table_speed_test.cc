// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.
//
// NOLINT(namespace-envoy)

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/symbol_table_impl.h"
#include "source/common/stats/utility.h"

#include "test/common/stats/make_elements_helper.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/blocking_counter.h"
#include "benchmark/benchmark.h"

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmCreateRace(benchmark::State& state) {
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Envoy::Thread::ThreadFactory& thread_factory = Envoy::Thread::threadFactoryForTest();

    // Make 100 threads, each of which will race to encode an overlapping set of
    // symbols, triggering corner-cases in SymbolTable::toSymbol.
    constexpr int num_threads = 100;
    std::vector<Envoy::Thread::ThreadPtr> threads;
    threads.reserve(num_threads);
    Envoy::ConditionalInitializer access, wait;
    absl::BlockingCounter accesses(num_threads);
    Envoy::Stats::SymbolTableImpl table;
    const absl::string_view stat_name_string = "here.is.a.stat.name";
    Envoy::Stats::StatNameStorage initial(stat_name_string, table);

    for (int i = 0; i < num_threads; ++i) {
      threads.push_back(
          thread_factory.createThread([&access, &accesses, &table, &stat_name_string]() {
            // Block each thread on waking up a common condition variable,
            // so we make it likely to race on access.
            access.wait();

            for (int count = 0; count < 1000; ++count) {
              Envoy::Stats::StatNameStorage second(stat_name_string, table);
              second.free(table);
            }
            accesses.DecrementCount();
          }));
    }

    // But when we access the already-existing symbols, we guarantee that no
    // further mutex contentions occur.
    access.setReady();
    accesses.Wait();

    for (auto& thread : threads) {
      thread->join();
    }

    initial.free(table);
  }
}
BENCHMARK(bmCreateRace)->Unit(::benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmJoinStatNames(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::IsolatedStoreImpl store(symbol_table);
  Envoy::Stats::StatNamePool pool(symbol_table);
  Envoy::Stats::StatName a = pool.add("a");
  Envoy::Stats::StatName b = pool.add("b");
  Envoy::Stats::StatName c = pool.add("c");
  Envoy::Stats::StatName d = pool.add("d");
  Envoy::Stats::StatName e = pool.add("e");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Envoy::Stats::Utility::counterFromStatNames(store, Envoy::Stats::makeStatNames(a, b, c, d, e));
  }
}
BENCHMARK(bmJoinStatNames);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmJoinElements(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::IsolatedStoreImpl store(symbol_table);
  Envoy::Stats::StatNamePool pool(symbol_table);
  Envoy::Stats::StatName a = pool.add("a");
  Envoy::Stats::StatName b = pool.add("b");
  Envoy::Stats::StatName c = pool.add("c");
  Envoy::Stats::StatName e = pool.add("e");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Envoy::Stats::Utility::counterFromElements(
        store, Envoy::Stats::makeElements(a, b, c, Envoy::Stats::DynamicName("d"), e));
  }
}
BENCHMARK(bmJoinElements);
