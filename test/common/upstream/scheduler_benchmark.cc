#include <algorithm>
#include <iostream>
#include <memory>
#include <random>

#include "source/common/common/random_generator.h"
#include "source/common/upstream/edf_scheduler.h"
#include "source/common/upstream/wrsq_scheduler.h"

#include "test/benchmark/main.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Upstream {
namespace {

class SchedulerTester {
public:
  struct ObjInfo {
    double weight;
  };

  static std::vector<std::shared_ptr<ObjInfo>>
  setupSplitWeights(Scheduler<ObjInfo>& sched, size_t num_objs, ::benchmark::State& state) {
    std::vector<std::shared_ptr<ObjInfo>> info;

    state.PauseTiming();
    for (uint32_t i = 0; i < num_objs; ++i) {
      auto oi = std::make_shared<ObjInfo>();
      if (i < num_objs / 2) {
        oi->weight = static_cast<double>(1);
      } else {
        oi->weight = static_cast<double>(4);
      }

      info.emplace_back(oi);
    }

    std::shuffle(info.begin(), info.end(), std::default_random_engine());
    state.ResumeTiming();

    for (auto& oi : info) {
      sched.add(oi->weight, oi);
    }

    return info;
  }

  static std::vector<std::shared_ptr<ObjInfo>>
  setupUniqueWeights(Scheduler<ObjInfo>& sched, size_t num_objs, ::benchmark::State& state) {
    std::vector<std::shared_ptr<ObjInfo>> info;

    state.PauseTiming();
    for (uint32_t i = 0; i < num_objs; ++i) {
      auto oi = std::make_shared<ObjInfo>();
      oi->weight = static_cast<double>(i + 1);

      info.emplace_back(oi);
    }

    std::shuffle(info.begin(), info.end(), std::default_random_engine());
    state.ResumeTiming();

    for (auto& oi : info) {
      sched.add(oi->weight, oi);
    }

    return info;
  }

  static void
  pickTest(Scheduler<ObjInfo>& sched, ::benchmark::State& state,
           std::function<std::vector<std::shared_ptr<ObjInfo>>(Scheduler<ObjInfo>&)> setup) {
    std::vector<std::shared_ptr<ObjInfo>> obj_info;
    for (auto _ : state) { // NOLINT: Silences warning about dead store
      if (obj_info.empty()) {
        obj_info = setup(sched);
      }

      sched.pickAndAdd([](const auto& i) { return i.weight; });
    }
  }
};

void splitWeightAddEdf(::benchmark::State& state) {
  EdfScheduler<SchedulerTester::ObjInfo> edf;
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupSplitWeights(edf, num_objs, state);
  }
}

void uniqueWeightAddEdf(::benchmark::State& state) {
  EdfScheduler<SchedulerTester::ObjInfo> edf;
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupUniqueWeights(edf, num_objs, state);
  }
}

void splitWeightPickEdf(::benchmark::State& state) {
  EdfScheduler<SchedulerTester::ObjInfo> edf;
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(edf, state,
                            [num_objs, &state](Scheduler<SchedulerTester::ObjInfo>& sched) {
                              return SchedulerTester::setupSplitWeights(sched, num_objs, state);
                            });
}

void uniqueWeightPickEdf(::benchmark::State& state) {
  EdfScheduler<SchedulerTester::ObjInfo> edf;
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(edf, state,
                            [num_objs, &state](Scheduler<SchedulerTester::ObjInfo>& sched) {
                              return SchedulerTester::setupUniqueWeights(sched, num_objs, state);
                            });
}

void splitWeightAddWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<SchedulerTester::ObjInfo> wrsq(random);
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupSplitWeights(wrsq, num_objs, state);
  }
}

void uniqueWeightAddWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<SchedulerTester::ObjInfo> wrsq(random);
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupUniqueWeights(wrsq, num_objs, state);
  }
}

void splitWeightPickWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<SchedulerTester::ObjInfo> wrsq(random);
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(wrsq, state,
                            [num_objs, &state](Scheduler<SchedulerTester::ObjInfo>& sched) {
                              return SchedulerTester::setupSplitWeights(sched, num_objs, state);
                            });
}

void uniqueWeightPickWRSQ(::benchmark::State& state) {
  Random::RandomGeneratorImpl random;
  WRSQScheduler<SchedulerTester::ObjInfo> wrsq(random);
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(wrsq, state,
                            [num_objs, &state](Scheduler<SchedulerTester::ObjInfo>& sched) {
                              return SchedulerTester::setupUniqueWeights(sched, num_objs, state);
                            });
}

BENCHMARK(splitWeightAddEdf)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(splitWeightAddWRSQ)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(splitWeightPickEdf)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);
BENCHMARK(splitWeightPickWRSQ)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);
BENCHMARK(uniqueWeightAddEdf)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(uniqueWeightAddWRSQ)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(8)
    ->Range(1 << 6, 1 << 14);
BENCHMARK(uniqueWeightPickEdf)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);
BENCHMARK(uniqueWeightPickWRSQ)->RangeMultiplier(8)->Range(1 << 6, 1 << 14);

} // namespace
} // namespace Upstream
} // namespace Envoy
