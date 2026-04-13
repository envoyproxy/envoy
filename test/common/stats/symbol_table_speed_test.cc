// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.
//
// NOLINT(namespace-envoy)

#include "source/common/common/hash.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/symbol_table.h"
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
              // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
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
    Envoy::Stats::Utility::counterFromStatNames(*store.rootScope(),
                                                Envoy::Stats::makeStatNames(a, b, c, d, e));
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
        *store.rootScope(), Envoy::Stats::makeElements(a, b, c, Envoy::Stats::DynamicName("d"), e));
  }
}
BENCHMARK(bmJoinElements);

static std::vector<Envoy::Stats::StatName> prepareNames(Envoy::Stats::StatNamePool& pool,
                                                        uint32_t num_words,
                                                        uint32_t num_tokens_per_word = 4) {
  const std::vector<absl::string_view> all_tokens = {
      "alpha", "beta",  "gamma",  "delta",   "epsilon", "zeta", "eta",     "theta",
      "iota",  "kappa", "lambda", "mu",      "nu",      "xi",   "omicron", "pi",
      "rho",   "sigma", "tau",    "upsilon", "phi",     "chi",  "psi",     "omega",
  };
  std::vector<Envoy::Stats::StatName> names;

  // Form token combinations selecting psuedo-randomly from the above set.
  for (uint32_t i = 0; i < num_words; ++i) {
    std::vector<absl::string_view> tokens;
    for (uint32_t j = 0; j < num_tokens_per_word; ++j) {
      const uint64_t seed = i * num_words + j;
      uint32_t index = Envoy::HashUtil::xxHash64("input", seed);
      tokens.push_back(all_tokens[index % all_tokens.size()]);
    }
    names.push_back(pool.add(absl::StrJoin(tokens, ".")));
  }
  return names;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmCompareElements(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool(symbol_table);
  const std::vector<Envoy::Stats::StatName> names = prepareNames(pool, 64);

  uint32_t compare_total = 0;

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    const uint64_t seed = compare_total++;
    uint32_t a = Envoy::HashUtil::xxHash64("first", seed) % names.size();
    uint32_t b = Envoy::HashUtil::xxHash64("second", seed) % names.size();
    benchmark::DoNotOptimize(symbol_table.lessThan(names[a], names[b]));
  }
}
BENCHMARK(bmCompareElements);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmSortByStatNames(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool(symbol_table);
  const std::vector<Envoy::Stats::StatName> names = prepareNames(pool, 100 * 1000);

  struct Getter {
    Envoy::Stats::StatName operator()(const Envoy::Stats::StatName& stat_name) const {
      return stat_name;
    }
  };
  Getter getter;

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Envoy::Stats::StatName> sort = names;
    symbol_table.sortByStatNames<Envoy::Stats::StatName>(sort.begin(), sort.end(), getter);
  }
}
BENCHMARK(bmSortByStatNames);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmStdSort(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool(symbol_table);
  const std::vector<Envoy::Stats::StatName> names = prepareNames(pool, 100 * 1000);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Envoy::Stats::StatName> sort = names;
    std::sort(sort.begin(), sort.end(), Envoy::Stats::StatNameLessThan(symbol_table));
  }
}
BENCHMARK(bmStdSort);

// Benchmarks StatName as a map key, exercising hash() + operator== on lookup.
// This covers the hot path in thread_local_store where counters/gauges are
// stored in StatNameHashMap and looked up via MetricHelper::statName().
// Uses 1000 entries with 20-token names to reflect production-scale stat names.
// NOLINTNEXTLINE(readability-identifier-naming)
static void bmStatNameMapLookup(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool(symbol_table);
  const std::vector<Envoy::Stats::StatName> names = prepareNames(pool, 2000, 24);

  Envoy::Stats::StatNameHashMap<uint64_t> map;
  for (uint64_t i = 0; i < names.size(); ++i) {
    map[names[i]] = i;
  }

  // Lookup each name -- the key is the same StatName pointer that was inserted,
  // so operator== hits the pointer-equal fast path.
  uint32_t index = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(map.find(names[index++ % names.size()]));
  }
}
BENCHMARK(bmStatNameMapLookup);

// Same as above but the lookup key is a different StatName with identical
// encoding, forcing operator== through the memcmp path.
// NOLINTNEXTLINE(readability-identifier-naming)
static void bmStatNameMapLookupDifferentPointer(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool_a(symbol_table);
  Envoy::Stats::StatNamePool pool_b(symbol_table);
  const std::vector<Envoy::Stats::StatName> names_a = prepareNames(pool_a, 2000, 24);
  const std::vector<Envoy::Stats::StatName> names_b = prepareNames(pool_b, 2000, 24);

  Envoy::Stats::StatNameHashMap<uint64_t> map;
  for (uint64_t i = 0; i < names_a.size(); ++i) {
    map[names_a[i]] = i;
  }

  // Lookup using names from pool_b -- same encoding but different pointers.
  uint32_t index = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(map.find(names_b[index++ % names_b.size()]));
  }
}
BENCHMARK(bmStatNameMapLookupDifferentPointer);

// Measures the hash+miss path where the key is not in the map.
// NOLINTNEXTLINE(readability-identifier-naming)
static void bmStatNameMapLookupMiss(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool_map(symbol_table);
  Envoy::Stats::StatNamePool pool_miss(symbol_table);
  const std::vector<Envoy::Stats::StatName> map_names = prepareNames(pool_map, 2000, 24);
  // Different token count produces names that won't be in the map.
  const std::vector<Envoy::Stats::StatName> miss_names = prepareNames(pool_miss, 2000, 25);

  Envoy::Stats::StatNameHashMap<uint64_t> map;
  for (uint64_t i = 0; i < map_names.size(); ++i) {
    map[map_names[i]] = i;
  }

  uint32_t index = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(map.find(miss_names[index++ % miss_names.size()]));
  }
}
BENCHMARK(bmStatNameMapLookupMiss);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmSortStrings(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool(symbol_table);
  const std::vector<Envoy::Stats::StatName> stat_names = prepareNames(pool, 100 * 1000);
  std::vector<std::string> names;
  names.reserve(stat_names.size());
  for (Envoy::Stats::StatName stat_name : stat_names) {
    names.emplace_back(symbol_table.toString(stat_name));
  }

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<std::string> sort = names;
    std::sort(sort.begin(), sort.end());
  }
}
BENCHMARK(bmSortStrings);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmSetStrings(benchmark::State& state) {
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Stats::StatNamePool pool(symbol_table);
  const std::vector<Envoy::Stats::StatName> stat_names = prepareNames(pool, 100 * 1000);
  std::vector<std::string> names;
  names.reserve(stat_names.size());
  for (Envoy::Stats::StatName stat_name : stat_names) {
    names.emplace_back(symbol_table.toString(stat_name));
  }

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::set<std::string> sorted(names.begin(), names.end());
    for (const std::string& str : sorted) {
      benchmark::DoNotOptimize(str);
    }
  }
}
BENCHMARK(bmSetStrings);
