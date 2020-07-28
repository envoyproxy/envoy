#include <iostream>
#include <string>

#include "common/common/fancy_logger.h"
#include "common/common/logger.h"

#include "benchmark/benchmark.h"

namespace Envoy {

/**
 * Benchmark for the main slow path, i.e. new logger creation here.
 */
static void fancySlowPath(benchmark::State& state) {
  FANCY_LOG(info, "Slow path test begins.");
  std::atomic<spdlog::logger*> logger;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      std::string key = "k" + std::to_string(i + (state.thread_index << 8));
      getFancyContext().initFancyLogger(key, logger);
    }
  }
}

#define FL FANCY_LOG(trace, "Default")
#define FL_8                                                                                       \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;
#define FL_64                                                                                      \
  { FL_8 FL_8 FL_8 FL_8 FL_8 FL_8 FL_8 FL_8 }
#define FL_512                                                                                     \
  { FL_64 FL_64 FL_64 FL_64 FL_64 FL_64 FL_64 FL_64 }
#define FL_1024                                                                                    \
  { FL_512 FL_512 }

/**
 * Benchmark for medium path, i.e. new site initialization within the same file.
 */
static void fancyMediumPath(benchmark::State& state) {
  FANCY_LOG(info, "Medium path test begins.");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    // create different call sites for medium path
    for (int i = 0; i < state.range(0); i++) {
      FL_1024
    }
  }
}

/**
 * Benchmark for fast path, i.e. integration test of common scenario.
 */
static void fancyFastPath(benchmark::State& state) {
  // control log length to be the same as normal Envoy below
  std::string msg(100 - strlen(__FILE__) + 4, '.');
  spdlog::level::level_enum lv = state.range(1) ? spdlog::level::trace : spdlog::level::info;
  getFancyContext().setFancyLogger(FANCY_KEY, lv);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      FANCY_LOG(trace, "Fast path: {}", msg);
    }
  }
}

/**
 * Benchmark for ENVOY_LOG to compare.
 */
static void envoyNormal(benchmark::State& state) {
  spdlog::level::level_enum lv = state.range(1) ? spdlog::level::trace : spdlog::level::info;
  std::string msg(100, '.');
  GET_MISC_LOGGER().set_level(lv);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      ENVOY_LOG_MISC(trace, "Fast path: {}", msg);
    }
  }
}

/**
 * Benchmark for a large number of level setting.
 */
static void fancyLevelSetting(benchmark::State& state) {
  FANCY_LOG(info, "Level setting test begins.");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      getFancyContext().setFancyLogger(__FILE__, spdlog::level::warn);
    }
  }
}

/**
 * Comparison with Envoy's level setting.
 */
static void envoyLevelSetting(benchmark::State& state) {
  ENVOY_LOG_MISC(info, "Envoy's level setting begins.");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      GET_MISC_LOGGER().set_level(spdlog::level::warn);
    }
  }
}

/**
 * Benchmarks in detail starts.
 */
BENCHMARK(fancySlowPath)->Arg(1 << 10);
BENCHMARK(fancySlowPath)->Arg(1 << 10)->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(fancySlowPath)->Arg(1 << 10)->Threads(200)->MeasureProcessCPUTime();

BENCHMARK(fancyMediumPath)->Arg(1)->Iterations(1);
// Seems medium path's concurrency test doesn't make sense (hard to do as well)

BENCHMARK(fancyFastPath)->Args({1024, 0})->Args({1024, 1}); // First no actual log, then log
BENCHMARK(fancyFastPath)->Args({1 << 10, 0})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(fancyFastPath)->Args({1 << 10, 1})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(fancyFastPath)->Args({1 << 10, 0})->Threads(200)->MeasureProcessCPUTime();
BENCHMARK(fancyFastPath)->Args({1 << 10, 1})->Threads(200)->MeasureProcessCPUTime();

BENCHMARK(envoyNormal)->Args({1024, 0})->Args({1024, 1});
BENCHMARK(envoyNormal)->Args({1 << 10, 0})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(envoyNormal)->Args({1 << 10, 1})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(envoyNormal)->Args({1 << 10, 0})->Threads(200)->MeasureProcessCPUTime();
BENCHMARK(envoyNormal)->Args({1 << 10, 1})->Threads(200)->MeasureProcessCPUTime();

BENCHMARK(fancyLevelSetting)->Arg(1 << 10);
BENCHMARK(envoyLevelSetting)->Arg(1 << 10);

} // namespace Envoy
