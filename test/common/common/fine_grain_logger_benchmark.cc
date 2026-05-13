#include <string>

#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"

#include "benchmark/benchmark.h"

namespace Envoy {

/**
 * Benchmark for the FINE_GRAIN_LOGGER macro with a STATIC name.
 * This measures the overhead of checkFineGrainLogger when it succeeds.
 */
static void BM_FineGrainStatic(benchmark::State& state) {
  spdlog::level::level_enum lv = state.range(0) ? spdlog::level::trace : spdlog::level::info;
  getFineGrainLogContext().setFineGrainLogger(__FILE__, lv);
  // Disable logging to avoid I/O overhead in benchmark
  auto logger = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  if (logger) {
    logger->set_level(spdlog::level::off);
  }
  for (auto _ : state) {
    FINE_GRAIN_LOG(trace, "Static message");
  }
}

/**
 * Benchmark for the FINE_GRAIN_LOGGER macro with a DYNAMIC name.
 * This measures the cost of switching loggers (correct behavior).
 */
static void BM_FineGrainDynamic(benchmark::State& state) {
  spdlog::level::level_enum lv = state.range(0) ? spdlog::level::trace : spdlog::level::info;
  const std::string name1 = "name1";
  const std::string name2 = "name2";
  const std::string key1 = absl::StrCat(__FILE__, ":", name1);
  const std::string key2 = absl::StrCat(__FILE__, ":", name2);
  getFineGrainLogContext().setFineGrainLogger(key1, lv);
  getFineGrainLogContext().setFineGrainLogger(key2, lv);
  
  // Ensure loggers exist and disable logging to avoid I/O overhead
  auto logger1 = getFineGrainLogContext().getFineGrainLogEntry(key1);
  if (logger1) logger1->set_level(spdlog::level::off);
  auto logger2 = getFineGrainLogContext().getFineGrainLogEntry(key2);
  if (logger2) logger2->set_level(spdlog::level::off);

  bool toggle = false;
  for (auto _ : state) {
    const std::string& name = toggle ? name1 : name2;
    FINE_GRAIN_GROUP_LOG(trace, name, "Dynamic message");
    toggle = !toggle;
  }
}

/**
 * Benchmark for the FINE_GRAIN_LOGGER macro with a DYNAMIC but STABLE name.
 * This measures the pointer-identity fast path.
 */
static void BM_FineGrainDynamicStable(benchmark::State& state) {
  spdlog::level::level_enum lv = state.range(0) ? spdlog::level::trace : spdlog::level::info;
  const std::string name = "stable_name";
  const std::string key = absl::StrCat(__FILE__, ":", name);
  getFineGrainLogContext().setFineGrainLogger(key, lv);
  
  // Disable logging to avoid I/O overhead in benchmark
  auto logger = getFineGrainLogContext().getFineGrainLogEntry(key);
  if (logger) {
    logger->set_level(spdlog::level::off);
  }

  for (auto _ : state) {
    FINE_GRAIN_GROUP_LOG(trace, name, "Stable dynamic message");
  }
}

/**
 * Benchmark for standard ENVOY_LOG_MISC (for comparison).
 */
static void BM_EnvoyLogMisc(benchmark::State& state) {
  spdlog::level::level_enum lv = state.range(0) ? spdlog::level::trace : spdlog::level::info;
  GET_MISC_LOGGER().set_level(lv);
  // Disable logging to avoid I/O overhead in benchmark
  GET_MISC_LOGGER().set_level(spdlog::level::off);
  for (auto _ : state) {
    ENVOY_LOG_MISC(trace, "Static message");
  }
}

// Range(0) = 0 means log level is INFO (trace log suppressed), 1 means log level is TRACE (log emitted).
BENCHMARK(BM_FineGrainStatic)->Args({0})->Args({1});
BENCHMARK(BM_FineGrainDynamicStable)->Args({0})->Args({1});
BENCHMARK(BM_FineGrainDynamic)->Args({0})->Args({1});
BENCHMARK(BM_EnvoyLogMisc)->Args({0})->Args({1});

} // namespace Envoy
