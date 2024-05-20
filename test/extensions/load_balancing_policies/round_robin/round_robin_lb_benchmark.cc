#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

#include "test/benchmark/main.h"
#include "test/extensions/load_balancing_policies/common/benchmark_base_tester.h"

namespace Envoy {
namespace Upstream {
namespace {

class RoundRobinTester : public BaseTester {
public:
  RoundRobinTester(uint64_t num_hosts, uint32_t weighted_subset_percent = 0, uint32_t weight = 0)
      : BaseTester(num_hosts, weighted_subset_percent, weight) {}

  void initialize() {
    lb_ = std::make_unique<RoundRobinLoadBalancer>(priority_set_, &local_priority_set_, stats_,
                                                   runtime_, random_, common_config_,
                                                   round_robin_lb_config_, simTime());
  }

  std::unique_ptr<RoundRobinLoadBalancer> lb_;
};

void benchmarkRoundRobinLoadBalancerBuild(::benchmark::State& state) {
  const uint64_t num_hosts = state.range(0);
  const uint64_t weighted_subset_percent = state.range(1);
  const uint64_t weight = state.range(2);

  if (benchmark::skipExpensiveBenchmarks() && num_hosts > 10000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    state.PauseTiming();
    const size_t start_tester_mem = Memory::Stats::totalCurrentlyAllocated();
    RoundRobinTester tester(num_hosts, weighted_subset_percent, weight);
    const size_t end_tester_mem = Memory::Stats::totalCurrentlyAllocated();
    const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();

    // We are only interested in timing the initial build.
    state.ResumeTiming();
    tester.initialize();
    state.PauseTiming();
    const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
    state.counters["tester_memory"] = end_tester_mem - start_tester_mem;
    state.counters["memory"] = end_mem - start_mem;
    state.counters["memory_per_host"] = (end_mem - start_mem) / num_hosts;
    state.ResumeTiming();
  }
}
BENCHMARK(benchmarkRoundRobinLoadBalancerBuild)
    ->Args({1, 0, 1})
    ->Args({500, 0, 1})
    ->Args({500, 50, 50})
    ->Args({500, 100, 50})
    ->Args({2500, 0, 1})
    ->Args({2500, 50, 50})
    ->Args({2500, 100, 50})
    ->Args({10000, 0, 1})
    ->Args({10000, 50, 50})
    ->Args({10000, 100, 50})
    ->Args({25000, 0, 1})
    ->Args({25000, 50, 50})
    ->Args({25000, 100, 50})
    ->Args({50000, 0, 1})
    ->Args({50000, 50, 50})
    ->Args({50000, 100, 50})
    ->Unit(::benchmark::kMillisecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
