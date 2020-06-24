#include <iostream>
#include <pthread.h>
#include <string>

#include "common/common/logger.h"
#include "benchmark/benchmark.h"

namespace Envoy {

/**
 * Benchmark for the main slow path, i.e. new logger creation.
 */
static void fancySlowPath(benchmark::State& state) {
  FANCY_LOG(info, "Slow path test begins.");
  std::atomic<spdlog::logger*> logger;
  for (auto _ : state) {
    for (int i = 0; i < state.range(0); i++) {
      std::string key = "k" + std::to_string(i + (state.thread_index << 8));
      initFancyLogger(key, logger);
    }
  }
}
BENCHMARK(fancySlowPath)->Arg(2 << 10);
BENCHMARK(fancySlowPath)->Arg(2 << 10)->Threads(4);

#define FL FANCY_LOG(trace, "Default")
#define FL_10                                                                                      \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;                                                                                              \
  FL;
#define FL_50                                                                                      \
  { FL_10 FL_10 FL_10 FL_10 FL_10 }
#define FL_250                                                                                     \
  { FL_50 FL_50 FL_50 FL_50 FL_50 }
#define FL_1000                                                                                    \
  { FL_250 FL_250 FL_250 FL_250 }

/**
 * Benchmark for medium path, i.e. new site initialization within the same file.
 */
static void fancyMediumPath(benchmark::State& state) {
  FANCY_LOG(info, "Medium path test begins.");
  for (auto _ : state) {
    for (int i = 0; i < state.range(0); i++) {
      FL_1000
    }
  }
}
BENCHMARK(fancyMediumPath)->Arg(1);
// Seems medium path's concurrency test doesn't make sense (hard to do as well)

/**
 * Benchmark for fast path, i.e. integration test of common scenario.
 */
static void fancyFastPath(benchmark::State& state) {
//   FANCY_LOG(info, "Fast path test begins.");
  spdlog::level::level_enum lv = state.range(1)? spdlog::level::trace : spdlog::level::info;
  setFancyLogger(FANCY_KEY, lv);
  for (auto _ : state) {
    for (int i = 0; i < state.range(0); i++) {
      FANCY_LOG(trace, "Fast path: {}", i);             // intended to disappear
    }
  }
}
BENCHMARK(fancyFastPath)->Args({30, 0})->Args({30, 1});   // First no actual log, then log
BENCHMARK(fancyFastPath)->Arg(1 << 8)->Threads(4);

/**
 * Benchmark for ENVOY_LOG to compare.
 */
static void envoyNormal(benchmark::State& state) {
    spdlog::level::level_enum lv = state.range(1)? spdlog::level::trace : spdlog::level::info;
    GET_MISC_LOGGER().set_level(lv);
    // ENVOY_LOG_MISC(info, "Envoy log begins.");
    for (auto _ : state) {
        for (int i = 0; i < state.range(0); i++) {
            ENVOY_LOG_MISC(trace, "Fast path: {}", i);
        }
    }
}
BENCHMARK(envoyNormal)->Args({30, 0})->Args({30, 1});
BENCHMARK(envoyNormal)->Arg(1 << 8)->Threads(4);

/**
 * Benchmark for a large number of level setting.
 */
static void fancyLevelSetting(benchmark::State& state) {
  FANCY_LOG(info, "Level setting test begins.");
  for (auto _ : state) {
    for (int i = 0; i < state.range(0); i++) {
        setFancyLogger(__FILE__, spdlog::level::warn);
    }
  }
}
BENCHMARK(fancyLevelSetting)->Arg(30);

/**
 * Comparison with Envoy's level setting.
 */
static void envoyLevelSetting(benchmark::State& state) {
    ENVOY_LOG_MISC(info, "Envoy's level setting begins.");
    for (auto _ : state) {
        for (int i = 0; i < state.range(0); i++) {
            GET_MISC_LOGGER().set_level(spdlog::level::warn);
        }
    }
}
BENCHMARK(envoyLevelSetting)->Arg(30);

} // namespace Envoy