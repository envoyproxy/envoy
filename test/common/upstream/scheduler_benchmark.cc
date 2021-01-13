// Usage: bazel run //test/common/upstream:iwrr_benchmark

#include <algorithm>
#include <iostream>
#include <memory>

#include "common/common/random_generator.h"
#include "common/upstream/edf_scheduler.h"
#include "common/upstream/wrsq_scheduler.h"

#include "test/benchmark/main.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Upstream {
namespace {

class SchedulerTester {
public:
  struct ObjInfo {
    std::shared_ptr<uint32_t> val;
    double weight;
  };

  static std::vector<ObjInfo> setupSplitWeights(Scheduler<uint32_t>& sched, size_t num_objs,
                                                ::benchmark::State& state) {
    std::vector<ObjInfo> info;

    state.PauseTiming();
    for (uint32_t i = 0; i < num_objs; ++i) {
      ObjInfo oi;
      oi.val = std::make_shared<uint32_t>(i);
      if (i < num_objs / 2) {
        oi.weight = static_cast<double>(1);
      } else {
        oi.weight = static_cast<double>(4);
      }

      info.emplace_back(oi);
    }

    std::random_shuffle(info.begin(), info.end());
    state.ResumeTiming();

    for (auto& oi : info) {
      sched.add(oi.weight, oi.val);
    }

    return info;
  }

  static std::vector<ObjInfo> setupUniqueWeights(Scheduler<uint32_t>& sched, size_t num_objs,
                                                 ::benchmark::State& state) {
    std::vector<ObjInfo> info;

    state.PauseTiming();
    for (uint32_t i = 0; i < num_objs; ++i) {
      ObjInfo oi;
      oi.val = std::make_shared<uint32_t>(i), oi.weight = static_cast<double>(i + 1),

      info.emplace_back(oi);
    }

    std::random_shuffle(info.begin(), info.end());
    state.ResumeTiming();

    for (auto& oi : info) {
      sched.add(oi.weight, oi.val);
    }

    return info;
  }

  static void pickTest(Scheduler<uint32_t>& sched, ::benchmark::State& state,
                       std::function<std::vector<ObjInfo>(Scheduler<uint32_t>&)> setup) {

    std::vector<ObjInfo> obj_info;
    for (auto _ : state) { // NOLINT: Silences warning about dead store
      if (obj_info.empty()) {
        obj_info = setup(sched);
      }

      auto p = sched.pickAndAdd([&obj_info](const auto& i) { return obj_info[i].weight; });
    }
  }
};

void BM_SplitWeightAddEdf(::benchmark::State& state) {
  EdfScheduler<uint32_t> edf;
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupSplitWeights(edf, num_objs, state);
  }
}

void BM_UniqueWeightAddEdf(::benchmark::State& state) {
  EdfScheduler<uint32_t> edf;
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupUniqueWeights(edf, num_objs, state);
  }
}

void BM_SplitWeightPickEdf(::benchmark::State& state) {
  EdfScheduler<uint32_t> edf;
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(edf, state, [num_objs, &state](Scheduler<uint32_t>& sched) {
    return SchedulerTester::setupSplitWeights(sched, num_objs, state);
  });
}

void BM_UniqueWeightPickEdf(::benchmark::State& state) {
  EdfScheduler<uint32_t> edf;
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(edf, state, [num_objs, &state](Scheduler<uint32_t>& sched) {
    return SchedulerTester::setupUniqueWeights(sched, num_objs, state);
  });
}

void BM_SplitWeightAddWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<uint32_t> wrsq(random);
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupSplitWeights(wrsq, num_objs, state);
  }
}

void BM_UniqueWeightAddWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<uint32_t> wrsq(random);
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupUniqueWeights(wrsq, num_objs, state);
  }
}

void BM_SplitWeightPickWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<uint32_t> wrsq(random);
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(wrsq, state, [num_objs, &state](Scheduler<uint32_t>& sched) {
    return SchedulerTester::setupSplitWeights(sched, num_objs, state);
  });
}

void BM_UniqueWeightPickWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<uint32_t> wrsq(random);
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(wrsq, state, [num_objs, &state](Scheduler<uint32_t>& sched) {
    return SchedulerTester::setupUniqueWeights(sched, num_objs, state);
  });
}

BENCHMARK(BM_SplitWeightAddEdf)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(BM_SplitWeightAddWRSQ)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(BM_SplitWeightPickEdf)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);
BENCHMARK(BM_SplitWeightPickWRSQ)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);
BENCHMARK(BM_UniqueWeightAddEdf)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(BM_UniqueWeightAddWRSQ)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(BM_UniqueWeightPickEdf)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);
BENCHMARK(BM_UniqueWeightPickWRSQ)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);

} // namespace
} // namespace Upstream
} // namespace Envoy
