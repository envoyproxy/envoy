#include <iostream>
#include <string>

#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"

#include "benchmark/benchmark.h"

namespace Envoy {

/**
 * Benchmark for the main slow path, i.e. new logger creation here.
 */
static void fineGrainLogSlowPath(benchmark::State& state) {
  FINE_GRAIN_LOG(info, "Slow path test begins.");
  std::atomic<spdlog::logger*> logger;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      std::string key = "k" + std::to_string(i + (state.thread_index() << 8));
      getFineGrainLogContext().initFineGrainLogger(key, logger);
    }
  }
}

#define FL FINE_GRAIN_LOG(trace, "Default")
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
static void fineGrainLogMediumPath(benchmark::State& state) {
  FINE_GRAIN_LOG(info, "Medium path test begins.");
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
static void fineGrainLogFastPath(benchmark::State& state) {
  // control log length to be the same as normal Envoy below
  std::string msg(100 - strlen(__FILE__) + 4, '.');
  spdlog::level::level_enum lv = state.range(1) ? spdlog::level::trace : spdlog::level::info;
  getFineGrainLogContext().setFineGrainLogger(__FILE__, lv);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      FINE_GRAIN_LOG(trace, "Fast path: {}", msg);
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
static void fineGrainLogLevelSetting(benchmark::State& state) {
  FINE_GRAIN_LOG(info, "Level setting test begins.");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::warn);
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
BENCHMARK(fineGrainLogSlowPath)->Arg(1 << 10);
BENCHMARK(fineGrainLogSlowPath)->Arg(1 << 10)->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(fineGrainLogSlowPath)->Arg(1 << 10)->Threads(200)->MeasureProcessCPUTime();

BENCHMARK(fineGrainLogMediumPath)->Arg(1)->Iterations(1);
// Seems medium path's concurrency test doesn't make sense (hard to do as well)

BENCHMARK(fineGrainLogFastPath)->Args({1024, 0})->Args({1024, 1}); // First no actual log, then log
BENCHMARK(fineGrainLogFastPath)->Args({1 << 10, 0})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(fineGrainLogFastPath)->Args({1 << 10, 1})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(fineGrainLogFastPath)->Args({1 << 10, 0})->Threads(200)->MeasureProcessCPUTime();
BENCHMARK(fineGrainLogFastPath)->Args({1 << 10, 1})->Threads(200)->MeasureProcessCPUTime();

BENCHMARK(envoyNormal)->Args({1024, 0})->Args({1024, 1});
BENCHMARK(envoyNormal)->Args({1 << 10, 0})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(envoyNormal)->Args({1 << 10, 1})->Threads(20)->MeasureProcessCPUTime();
BENCHMARK(envoyNormal)->Args({1 << 10, 0})->Threads(200)->MeasureProcessCPUTime();
BENCHMARK(envoyNormal)->Args({1 << 10, 1})->Threads(200)->MeasureProcessCPUTime();

BENCHMARK(fineGrainLogLevelSetting)->Arg(1 << 10);
BENCHMARK(envoyLevelSetting)->Arg(1 << 10);

} // namespace Envoy
