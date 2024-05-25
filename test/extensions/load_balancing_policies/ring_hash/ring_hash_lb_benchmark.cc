#include "source/extensions/load_balancing_policies/ring_hash/ring_hash_lb.h"

#include "test/benchmark/main.h"
#include "test/extensions/load_balancing_policies/common/benchmark_base_tester.h"

namespace Envoy {
namespace Upstream {
namespace {

class RingHashTester : public BaseTester {
public:
  RingHashTester(uint64_t num_hosts, uint64_t min_ring_size) : BaseTester(num_hosts) {
    config_ = envoy::config::cluster::v3::Cluster::RingHashLbConfig();
    config_.value().mutable_minimum_ring_size()->set_value(min_ring_size);
    ring_hash_lb_ = std::make_unique<RingHashLoadBalancer>(
        priority_set_, stats_, stats_scope_, runtime_, random_,
        config_.has_value()
            ? makeOptRef<const envoy::config::cluster::v3::Cluster::RingHashLbConfig>(
                  config_.value())
            : absl::nullopt,
        common_config_);
  }

  absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig> config_;
  std::unique_ptr<RingHashLoadBalancer> ring_hash_lb_;
};

void benchmarkRingHashLoadBalancerBuildRing(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t min_ring_size = state.range(1);
    RingHashTester tester(num_hosts, min_ring_size);

    const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();

    // We are only interested in timing the initial ring build.
    state.ResumeTiming();
    ASSERT_TRUE(tester.ring_hash_lb_->initialize().ok());
    state.PauseTiming();
    const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
    state.counters["memory"] = end_mem - start_mem;
    state.counters["memory_per_host"] = (end_mem - start_mem) / num_hosts;
    state.ResumeTiming();
  }
}
BENCHMARK(benchmarkRingHashLoadBalancerBuildRing)
    ->Args({100, 65536})
    ->Args({200, 65536})
    ->Args({500, 65536})
    ->Args({100, 256000})
    ->Args({200, 256000})
    ->Args({500, 256000})
    ->Unit(::benchmark::kMillisecond);

void benchmarkRingHashLoadBalancerChooseHost(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    // Do not time the creation of the ring.
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t min_ring_size = state.range(1);
    const uint64_t keys_to_simulate = state.range(2);
    RingHashTester tester(num_hosts, min_ring_size);
    ASSERT_TRUE(tester.ring_hash_lb_->initialize().ok());
    LoadBalancerPtr lb = tester.ring_hash_lb_->factory()->create(tester.lb_params_);
    absl::node_hash_map<std::string, uint64_t> hit_counter;
    TestLoadBalancerContext context;
    state.ResumeTiming();

    // Note: To a certain extent this is benchmarking the performance of xxhash as well as
    // absl::node_hash_map. However, it should be roughly equivalent to the work done when
    // comparing different hashing algorithms.
    // TODO(mattklein123): When Maglev is a real load balancer, further share code with the
    //                     other test.
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
BENCHMARK(benchmarkRingHashLoadBalancerChooseHost)
    ->Args({100, 65536, 100000})
    ->Args({200, 65536, 100000})
    ->Args({500, 65536, 100000})
    ->Args({100, 256000, 100000})
    ->Args({200, 256000, 100000})
    ->Args({500, 256000, 100000})
    ->Unit(::benchmark::kMillisecond);

void benchmarkRingHashLoadBalancerHostLoss(::benchmark::State& state) {
  const uint64_t num_hosts = state.range(0);
  const uint64_t min_ring_size = state.range(1);
  const uint64_t hosts_to_lose = state.range(2);
  const uint64_t keys_to_simulate = state.range(3);

  if (benchmark::skipExpensiveBenchmarks() && min_ring_size > 65536) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    RingHashTester tester(num_hosts, min_ring_size);
    ASSERT_TRUE(tester.ring_hash_lb_->initialize().ok());
    LoadBalancerPtr lb = tester.ring_hash_lb_->factory()->create(tester.lb_params_);
    std::vector<HostConstSharedPtr> hosts;
    TestLoadBalancerContext context;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts.push_back(lb->chooseHost(&context));
    }

    RingHashTester tester2(num_hosts - hosts_to_lose, min_ring_size);
    ASSERT_TRUE(tester2.ring_hash_lb_->initialize().ok());
    lb = tester2.ring_hash_lb_->factory()->create(tester2.lb_params_);
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
BENCHMARK(benchmarkRingHashLoadBalancerHostLoss)
    ->Args({500, 65536, 1, 10000})
    ->Args({500, 65536, 2, 10000})
    ->Args({500, 65536, 3, 10000})
    ->Args({500, 256000, 1, 10000})
    ->Args({500, 256000, 2, 10000})
    ->Args({500, 256000, 3, 10000})
    ->Unit(::benchmark::kMillisecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
