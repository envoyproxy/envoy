// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.
//
// NOLINT(namespace-envoy)

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/stats/isolated_store_impl.h"
#include "common/stats/symbol_table_impl.h"
#include "common/stats/utility.h"

#include "test/common/stats/make_elements_helper.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/blocking_counter.h"
#include "benchmark/benchmark.h"

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_CreateRace(benchmark::State& state) {
  Envoy::Thread::ThreadFactory& thread_factory = Envoy::Thread::threadFactoryForTest();

  // Make 100 threads, each of which will race to encode an overlapping set of
  // symbols, triggering corner-cases in SymbolTable::toSymbol.
  constexpr int num_threads = 36;
  std::vector<Envoy::Thread::ThreadPtr> threads;
  threads.reserve(num_threads);
  Envoy::ConditionalInitializer access, wait;
  absl::BlockingCounter accesses(num_threads);
  Envoy::Stats::SymbolTableImpl table;
  const absl::string_view stat_name_string = "here.is.a.stat.name";
  Envoy::Stats::StatNameStorage initial(stat_name_string, table);

  for (int i = 0; i < num_threads; ++i) {
    threads.push_back(
        thread_factory.createThread([&access, &accesses, &state, &table, &stat_name_string]() {
          // Block each thread on waking up a common condition variable,
          // so we make it likely to race on access.
          access.wait();

          for (auto _ : state) {
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
BENCHMARK(BM_CreateRace);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JoinStatNames(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::IsolatedStoreImpl store(symbol_table);
  Envoy::Stats::StatNamePool pool(symbol_table);
  Envoy::Stats::StatName a = pool.add("a");
  Envoy::Stats::StatName b = pool.add("b");
  Envoy::Stats::StatName c = pool.add("c");
  Envoy::Stats::StatName d = pool.add("d");
  Envoy::Stats::StatName e = pool.add("e");
  for (auto _ : state) {
    Envoy::Stats::Utility::counterFromStatNames(store, Envoy::Stats::makeStatNames(a, b, c, d, e));
  }
}
BENCHMARK(BM_JoinStatNames);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JoinElements(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::IsolatedStoreImpl store(symbol_table);
  Envoy::Stats::StatNamePool pool(symbol_table);
  Envoy::Stats::StatName a = pool.add("a");
  Envoy::Stats::StatName b = pool.add("b");
  Envoy::Stats::StatName c = pool.add("c");
  Envoy::Stats::StatName e = pool.add("e");
  for (auto _ : state) {
    Envoy::Stats::Utility::counterFromElements(
        store, Envoy::Stats::makeElements(a, b, c, Envoy::Stats::DynamicName("d"), e));
  }
}
BENCHMARK(BM_JoinElements);

int main(int argc, char** argv) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logger_context(spdlog::level::warn,
                                        Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
