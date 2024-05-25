#include "source/extensions/load_balancing_policies/maglev/maglev_lb.h"

#include "test/benchmark/main.h"
#include "test/extensions/load_balancing_policies/common/benchmark_base_tester.h"

namespace Envoy {
namespace Upstream {
namespace {

class MaglevTester : public BaseTester {
public:
  MaglevTester(uint64_t num_hosts, uint32_t weighted_subset_percent = 0, uint32_t weight = 0)
      : BaseTester(num_hosts, weighted_subset_percent, weight) {
    maglev_lb_ = std::make_unique<MaglevLoadBalancer>(
        priority_set_, stats_, stats_scope_, runtime_, random_,
        config_.has_value()
            ? makeOptRef<const envoy::config::cluster::v3::Cluster::MaglevLbConfig>(config_.value())
            : absl::nullopt,
        common_config_);
  }

  absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig> config_;
  std::unique_ptr<MaglevLoadBalancer> maglev_lb_;
};

void benchmarkMaglevLoadBalancerChooseHost(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    // Do not time the creation of the table.
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t keys_to_simulate = state.range(1);
    MaglevTester tester(num_hosts);
    ASSERT_TRUE(tester.maglev_lb_->initialize().ok());
    LoadBalancerPtr lb = tester.maglev_lb_->factory()->create(tester.lb_params_);
    absl::node_hash_map<std::string, uint64_t> hit_counter;
    TestLoadBalancerContext context;
    state.ResumeTiming();

    // Note: To a certain extent this is benchmarking the performance of xxhash as well as
    // absl::node_hash_map. However, it should be roughly equivalent to the work done when
    // comparing different hashing algorithms.
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hit_counter[lb->chooseHost(&context)->address()->asString()] += 1;
    }

    // Do not time computation of mean, standard deviation, and relative standard deviation.
    state.PauseTiming();
    computeHitStats(state, hit_counter);
    state.ResumeTiming();
  }
}
BENCHMARK(benchmarkMaglevLoadBalancerChooseHost)
    ->Args({100, 100000})
    ->Args({200, 100000})
    ->Args({500, 100000})
    ->Unit(::benchmark::kMillisecond);

void benchmarkMaglevLoadBalancerBuildTable(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    MaglevTester tester(num_hosts);

    const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();

    // We are only interested in timing the initial table build.
    state.ResumeTiming();
    ASSERT_TRUE(tester.maglev_lb_->initialize().ok());
    state.PauseTiming();
    const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
    state.counters["memory"] = end_mem - start_mem;
    state.counters["memory_per_host"] = (end_mem - start_mem) / num_hosts;
    state.ResumeTiming();
  }
}
BENCHMARK(benchmarkMaglevLoadBalancerBuildTable)
    ->Arg(100)
    ->Arg(200)
    ->Arg(500)
    ->Unit(::benchmark::kMillisecond);

void benchmarkMaglevLoadBalancerHostLoss(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    const uint64_t num_hosts = state.range(0);
    const uint64_t hosts_to_lose = state.range(1);
    const uint64_t keys_to_simulate = state.range(2);

    MaglevTester tester(num_hosts);
    ASSERT_TRUE(tester.maglev_lb_->initialize().ok());
    LoadBalancerPtr lb = tester.maglev_lb_->factory()->create(tester.lb_params_);
    std::vector<HostConstSharedPtr> hosts;
    TestLoadBalancerContext context;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts.push_back(lb->chooseHost(&context));
    }

    MaglevTester tester2(num_hosts - hosts_to_lose);
    ASSERT_TRUE(tester2.maglev_lb_->initialize().ok());
    lb = tester2.maglev_lb_->factory()->create(tester2.lb_params_);
    std::vector<HostConstSharedPtr> hosts2;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts2.push_back(lb->chooseHost(&context));
    }

    ASSERT(hosts.size() == hosts2.size());
    uint64_t num_different_hosts = 0;
    for (uint64_t i = 0; i < hosts.size(); i++) {
      if (hosts[i]->address()->asString() != hosts2[i]->address()->asString()) {
        num_different_hosts++;
      }
    }

    state.counters["percent_different"] =
        (static_cast<double>(num_different_hosts) / hosts.size()) * 100;
    state.counters["host_loss_over_N_optimal"] =
        (static_cast<double>(hosts_to_lose) / num_hosts) * 100;
  }
}
BENCHMARK(benchmarkMaglevLoadBalancerHostLoss)
    ->Args({500, 1, 10000})
    ->Args({500, 2, 10000})
    ->Args({500, 3, 10000})
    ->Unit(::benchmark::kMillisecond);

void benchmarkMaglevLoadBalancerWeighted(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    const uint64_t num_hosts = state.range(0);
    const uint64_t weighted_subset_percent = state.range(1);
    const uint64_t before_weight = state.range(2);
    const uint64_t after_weight = state.range(3);
    const uint64_t keys_to_simulate = state.range(4);

    MaglevTester tester(num_hosts, weighted_subset_percent, before_weight);
    ASSERT_TRUE(tester.maglev_lb_->initialize().ok());
    LoadBalancerPtr lb = tester.maglev_lb_->factory()->create(tester.lb_params_);
    std::vector<HostConstSharedPtr> hosts;
    TestLoadBalancerContext context;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts.push_back(lb->chooseHost(&context));
    }

    MaglevTester tester2(num_hosts, weighted_subset_percent, after_weight);
    ASSERT_TRUE(tester2.maglev_lb_->initialize().ok());
    lb = tester2.maglev_lb_->factory()->create(tester2.lb_params_);
    std::vector<HostConstSharedPtr> hosts2;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts2.push_back(lb->chooseHost(&context));
    }

    ASSERT(hosts.size() == hosts2.size());
    uint64_t num_different_hosts = 0;
    for (uint64_t i = 0; i < hosts.size(); i++) {
      if (hosts[i]->address()->asString() != hosts2[i]->address()->asString()) {
        num_different_hosts++;
      }
    }

    state.counters["percent_different"] =
        (static_cast<double>(num_different_hosts) / hosts.size()) * 100;
    const auto weighted_hosts_percent = [weighted_subset_percent](uint32_t weight) -> double {
      const double weighted_hosts = weighted_subset_percent;
      const double unweighted_hosts = 100.0 - weighted_hosts;
      const double total_weight = weighted_hosts * weight + unweighted_hosts;
      return 100.0 * (weighted_hosts * weight) / total_weight;
    };
    state.counters["optimal_percent_different"] =
        std::abs(weighted_hosts_percent(before_weight) - weighted_hosts_percent(after_weight));
  }
}
BENCHMARK(benchmarkMaglevLoadBalancerWeighted)
    ->Args({500, 5, 1, 1, 10000})
    ->Args({500, 5, 1, 127, 1000})
    ->Args({500, 5, 127, 1, 10000})
    ->Args({500, 50, 1, 127, 1000})
    ->Args({500, 50, 127, 1, 10000})
    ->Args({500, 95, 1, 127, 1000})
    ->Args({500, 95, 127, 1, 10000})
    ->Args({500, 95, 25, 75, 1000})
    ->Args({500, 95, 75, 25, 10000})
    ->Unit(::benchmark::kMillisecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
