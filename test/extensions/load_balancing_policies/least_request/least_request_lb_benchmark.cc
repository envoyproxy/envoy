#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"

#include "test/benchmark/main.h"
#include "test/extensions/load_balancing_policies/common/benchmark_base_tester.h"

namespace Envoy {
namespace Upstream {
namespace {

class LeastRequestTester : public BaseTester {
public:
  LeastRequestTester(uint64_t num_hosts, uint32_t choice_count) : BaseTester(num_hosts) {
    envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
    lr_lb_config.mutable_choice_count()->set_value(choice_count);
    lb_ = std::make_unique<LeastRequestLoadBalancer>(priority_set_, &local_priority_set_, stats_,
                                                     runtime_, random_, common_config_,
                                                     lr_lb_config, simTime());
  }

  std::unique_ptr<LeastRequestLoadBalancer> lb_;
};

void benchmarkLeastRequestLoadBalancerChooseHost(::benchmark::State& state) {
  const uint64_t num_hosts = state.range(0);
  const uint64_t choice_count = state.range(1);
  const uint64_t keys_to_simulate = state.range(2);

  if (benchmark::skipExpensiveBenchmarks() && keys_to_simulate > 1000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    state.PauseTiming();
    LeastRequestTester tester(num_hosts, choice_count);
    absl::node_hash_map<std::string, uint64_t> hit_counter;
    TestLoadBalancerContext context;
    state.ResumeTiming();

    for (uint64_t i = 0; i < keys_to_simulate; ++i) {
      hit_counter[tester.lb_->chooseHost(&context)->address()->asString()] += 1;
    }

    // Do not time computation of mean, standard deviation, and relative standard deviation.
    state.PauseTiming();
    computeHitStats(state, hit_counter);
    state.ResumeTiming();
  }
}
BENCHMARK(benchmarkLeastRequestLoadBalancerChooseHost)
    ->Args({100, 1, 1000})
    ->Args({100, 2, 1000})
    ->Args({100, 3, 1000})
    ->Args({100, 10, 1000})
    ->Args({100, 50, 1000})
    ->Args({100, 100, 1000})
    ->Args({100, 1, 1000000})
    ->Args({100, 2, 1000000})
    ->Args({100, 3, 1000000})
    ->Args({100, 10, 1000000})
    ->Args({100, 50, 1000000})
    ->Args({100, 100, 1000000})
    ->Unit(::benchmark::kMillisecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
