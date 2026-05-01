// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.
//
// NOLINT(namespace-envoy)

#include <array>
#include <bit>

#include "source/common/common/hash.h"
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

  // Form token combinations selecting pseudo-randomly from the above set.
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

// ---------------------------------------------------------------------------
// decodeNumber micro-benchmarks
//
// These benchmarks isolate the varint decode function with different
// implementations so we can measure the cost of the fast-path branch,
// loop overhead, etc. Values are parameterized across varint-size
// boundaries: 1-byte (<=127), 2-byte (128-16383), 3-byte (16384-2097151).
// ---------------------------------------------------------------------------

using Encoding = Envoy::Stats::SymbolTable::Encoding;
static constexpr size_t MaxVarintBytes = 10;

// Encode a varint into buf[]. Returns the number of bytes written.
static size_t encodeNumberToBuffer(uint64_t number, uint8_t* buf) {
  size_t len = 0;
  do {
    if (number < Encoding::SpilloverMask) {
      buf[len++] = static_cast<uint8_t>(number);
    } else {
      buf[len++] = static_cast<uint8_t>((number & Encoding::Low7Bits) | Encoding::SpilloverMask);
    }
    number >>= 7;
  } while (number != 0);
  return len;
}

// Implementation A: fast-path + loop (matches the current header).
static std::pair<uint64_t, size_t> decodeNumberFastPath(const uint8_t* encoding) {
  if (encoding[0] < Encoding::SpilloverMask) {
    return {encoding[0], 1};
  }
  uint64_t number = 0;
  uint64_t uc = Encoding::SpilloverMask;
  const uint8_t* start = encoding;
  for (uint32_t shift = 0; (uc & Encoding::SpilloverMask) != 0; ++encoding, shift += 7) {
    uc = static_cast<uint32_t>(*encoding);
    number |= (uc & Encoding::Low7Bits) << shift;
  }
  return {number, static_cast<size_t>(encoding - start)};
}

// Implementation B: loop-only (the original, no fast-path check).
static std::pair<uint64_t, size_t> decodeNumberLoopOnly(const uint8_t* encoding) {
  uint64_t number = 0;
  uint64_t uc = Encoding::SpilloverMask;
  const uint8_t* start = encoding;
  for (uint32_t shift = 0; (uc & Encoding::SpilloverMask) != 0; ++encoding, shift += 7) {
    uc = static_cast<uint32_t>(*encoding);
    number |= (uc & Encoding::Low7Bits) << shift;
  }
  return {number, static_cast<size_t>(encoding - start)};
}

// Implementation C: do-while with index instead of pointer arithmetic.
static std::pair<uint64_t, size_t> decodeNumberDoWhile(const uint8_t* encoding) {
  uint64_t res = 0;
  uint64_t byte;
  size_t i = 0;
  do {
    byte = encoding[i];
    res |= (byte & Encoding::Low7Bits) << (7 * i);
    i++;
  } while ((byte & Encoding::SpilloverMask) && i < MaxVarintBytes);
  return {res, i};
}

// Implementation D: unrolled with early returns per byte.
static std::pair<uint64_t, size_t> decodeNumberUnrolled(const uint8_t* p) {
  if (p[0] < Encoding::SpilloverMask) {
    return {p[0], 1};
  }
  uint64_t res = p[0] & Encoding::Low7Bits;
  if (p[1] < Encoding::SpilloverMask) {
    return {res | static_cast<uint64_t>(p[1]) << 7, 2};
  }
  res |= static_cast<uint64_t>(p[1] & Encoding::Low7Bits) << 7;
  if (p[2] < Encoding::SpilloverMask) {
    return {res | static_cast<uint64_t>(p[2]) << 14, 3};
  }
  // Fall back to loop for 4+ byte varint (values >= 2^21).
  res |= static_cast<uint64_t>(p[2] & Encoding::Low7Bits) << 14;
  for (size_t i = 3; i < MaxVarintBytes; ++i) {
    if (p[i] < Encoding::SpilloverMask) {
      return {res | static_cast<uint64_t>(p[i]) << (7 * i), i + 1};
    }
    res |= static_cast<uint64_t>(p[i] & Encoding::Low7Bits) << (7 * i);
  }
  return {res, MaxVarintBytes};
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_FastPath(benchmark::State& state) {
  uint8_t buf[MaxVarintBytes];
  encodeNumberToBuffer(static_cast<uint64_t>(state.range(0)), buf);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberFastPath(buf);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_FastPath)
    ->Arg(10)
    ->Arg(127)
    ->Arg(128)
    ->Arg(16383)
    ->Arg(16384)
    ->Arg(100000)
    ->Arg(2097151)
    ->Arg(2097152);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_LoopOnly(benchmark::State& state) {
  uint8_t buf[MaxVarintBytes];
  encodeNumberToBuffer(static_cast<uint64_t>(state.range(0)), buf);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberLoopOnly(buf);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_LoopOnly)
    ->Arg(10)
    ->Arg(127)
    ->Arg(128)
    ->Arg(16383)
    ->Arg(16384)
    ->Arg(100000)
    ->Arg(2097151)
    ->Arg(2097152);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_DoWhile(benchmark::State& state) {
  uint8_t buf[MaxVarintBytes];
  encodeNumberToBuffer(static_cast<uint64_t>(state.range(0)), buf);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberDoWhile(buf);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_DoWhile)
    ->Arg(10)
    ->Arg(127)
    ->Arg(128)
    ->Arg(16383)
    ->Arg(16384)
    ->Arg(100000)
    ->Arg(2097151)
    ->Arg(2097152);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_Unrolled(benchmark::State& state) {
  uint8_t buf[MaxVarintBytes];
  encodeNumberToBuffer(static_cast<uint64_t>(state.range(0)), buf);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberUnrolled(buf);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_Unrolled)
    ->Arg(10)
    ->Arg(127)
    ->Arg(128)
    ->Arg(16383)
    ->Arg(16384)
    ->Arg(100000)
    ->Arg(2097151)
    ->Arg(2097152);

// Pre-encoded varint buffers for mixed-distribution benchmarks.
// ~80% values <= 127 (1-byte fast-path), ~20% values > 127 (multi-byte).
static std::vector<std::array<uint8_t, MaxVarintBytes>> prepareDecodeNumberMixedBuffers() {
  const uint64_t values[] = {1, 42, 5, 100, 200, 77, 13, 63, 500, 120, 50000};
  constexpr size_t num_entries = 1024;
  std::vector<std::array<uint8_t, MaxVarintBytes>> buffers(num_entries);
  for (size_t i = 0; i < num_entries; ++i) {
    buffers[i].fill(0);
    encodeNumberToBuffer(values[i % 11], buffers[i].data());
  }
  return buffers;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_FastPathMixed(benchmark::State& state) {
  const auto buffers = prepareDecodeNumberMixedBuffers();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    const auto& buf = buffers[idx++ % buffers.size()];
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberFastPath(buf.data());
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_FastPathMixed);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_LoopOnlyMixed(benchmark::State& state) {
  const auto buffers = prepareDecodeNumberMixedBuffers();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    const auto& buf = buffers[idx++ % buffers.size()];
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberLoopOnly(buf.data());
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_LoopOnlyMixed);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_DoWhileMixed(benchmark::State& state) {
  const auto buffers = prepareDecodeNumberMixedBuffers();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    const auto& buf = buffers[idx++ % buffers.size()];
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberDoWhile(buf.data());
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_DoWhileMixed);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmDecodeNumber_UnrolledMixed(benchmark::State& state) {
  const auto buffers = prepareDecodeNumberMixedBuffers();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    const auto& buf = buffers[idx++ % buffers.size()];
    benchmark::DoNotOptimize(buf);
    auto result = decodeNumberUnrolled(buf.data());
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmDecodeNumber_UnrolledMixed);

// ---------------------------------------------------------------------------
// encodingSizeBytes micro-benchmarks
//
// Compare implementations of the varint size calculation: loop, hybrid
// (branch + count-leading-zero), loop-with-early-exit, and pure count-
// leading-zero (branch-free). Two value distributions: short (1-byte varint)
// and mixed (1-5 byte).
// ---------------------------------------------------------------------------

static size_t encodingSizeBytesLoop(uint64_t number) {
  size_t num_bytes = 0;
  do {
    ++num_bytes;
    number >>= 7;
  } while (number != 0);
  return num_bytes;
}

static size_t encodingSizeBytesHybrid(uint64_t number) {
  if (number < Encoding::SpilloverMask) {
    return 1;
  }
  return (64 - std::countl_zero(number) + 6) / 7;
}

static size_t encodingSizeBytesLoopEarly(uint64_t number) {
  if (number < Encoding::SpilloverMask) {
    return 1;
  }
  size_t num_bytes = 0;
  do {
    ++num_bytes;
    number >>= 7;
  } while (number != 0);
  return num_bytes;
}

static size_t encodingSizeBytesClz(uint64_t number) {
  return (64 - std::countl_zero(number | 1) + 6) / 7;
}

static std::vector<uint64_t> prepareShortValues() {
  std::vector<uint64_t> values(1024);
  for (size_t i = 0; i < values.size(); ++i) {
    values[i] = (i * 7 + 13) % 127 + 1;
  }
  return values;
}

static std::vector<uint64_t> prepareMixedValues() {
  std::vector<uint64_t> values(1024);
  const uint64_t representatives[] = {42, 200, 100000, 0x1000000, 0x100000000ULL};
  for (size_t i = 0; i < values.size(); ++i) {
    values[i] = representatives[i % 5];
  }
  return values;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_HybridShort(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareShortValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesHybrid(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_HybridShort);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_HybridMixed(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareMixedValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesHybrid(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_HybridMixed);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_LoopShort(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareShortValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesLoop(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_LoopShort);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_LoopMixed(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareMixedValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesLoop(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_LoopMixed);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_LoopEarlyShort(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareShortValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesLoopEarly(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_LoopEarlyShort);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_LoopEarlyMixed(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareMixedValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesLoopEarly(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_LoopEarlyMixed);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_ClzShort(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareShortValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesClz(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_ClzShort);

// NOLINTNEXTLINE(readability-identifier-naming)
static void bmEncodingSizeBytes_ClzMixed(benchmark::State& state) {
  const std::vector<uint64_t> vals = prepareMixedValues();
  uint32_t idx = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(encodingSizeBytesClz(vals[idx++ % vals.size()]));
  }
}
BENCHMARK(bmEncodingSizeBytes_ClzMixed);
