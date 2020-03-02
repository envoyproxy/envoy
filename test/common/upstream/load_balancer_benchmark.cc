// Usage: bazel run //test/common/upstream:load_balancer_benchmark

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "common/memory/stats.h"
#include "common/runtime/runtime_impl.h"
#include "common/upstream/maglev_lb.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Upstream {
namespace {

class BaseTester {
public:
  // We weight the first weighted_subset_percent of hosts with weight.
  BaseTester(uint64_t num_hosts, uint32_t weighted_subset_percent = 0, uint32_t weight = 0) {
    HostVector hosts;
    ASSERT(num_hosts < 65536);
    for (uint64_t i = 0; i < num_hosts; i++) {
      const bool should_weight = i < num_hosts * (weighted_subset_percent / 100.0);
      hosts.push_back(makeTestHost(info_, fmt::format("tcp://10.0.{}.{}:6379", i / 256, i % 256),
                                   should_weight ? weight : 1));
    }

    HostVectorConstSharedPtr updated_hosts = std::make_shared<HostVector>(hosts);
    HostsPerLocalityConstSharedPtr hosts_per_locality = makeHostsPerLocality({hosts});
    priority_set_.updateHosts(0, HostSetImpl::partitionHosts(updated_hosts, hosts_per_locality), {},
                              hosts, {}, absl::nullopt);
    local_priority_set_.updateHosts(0,
                                    HostSetImpl::partitionHosts(updated_hosts, hosts_per_locality),
                                    {}, hosts, {}, absl::nullopt);
  }

  PrioritySetImpl priority_set_;
  PrioritySetImpl local_priority_set_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_{ClusterInfoImpl::generateStats(stats_store_)};
  NiceMock<Runtime::MockLoader> runtime_;
  Runtime::RandomGeneratorImpl random_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
};

class RoundRobinTester : public BaseTester {
public:
  RoundRobinTester(uint64_t num_hosts, uint32_t weighted_subset_percent = 0, uint32_t weight = 0)
      : BaseTester(num_hosts, weighted_subset_percent, weight) {}

  void initialize() {
    lb_ = std::make_unique<RoundRobinLoadBalancer>(priority_set_, &local_priority_set_, stats_,
                                                   runtime_, random_, common_config_);
  }

  std::unique_ptr<RoundRobinLoadBalancer> lb_;
};

class LeastRequestTester : public BaseTester {
public:
  LeastRequestTester(uint64_t num_hosts, uint32_t choice_count) : BaseTester(num_hosts) {
    envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
    lr_lb_config.mutable_choice_count()->set_value(choice_count);
    lb_ =
        std::make_unique<LeastRequestLoadBalancer>(priority_set_, &local_priority_set_, stats_,
                                                   runtime_, random_, common_config_, lr_lb_config);
  }

  std::unique_ptr<LeastRequestLoadBalancer> lb_;
};

void BM_RoundRobinLoadBalancerBuild(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t weighted_subset_percent = state.range(1);
    const uint64_t weight = state.range(2);

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
BENCHMARK(BM_RoundRobinLoadBalancerBuild)
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
    ->Unit(benchmark::kMillisecond);

class RingHashTester : public BaseTester {
public:
  RingHashTester(uint64_t num_hosts, uint64_t min_ring_size) : BaseTester(num_hosts) {
    config_ = envoy::config::cluster::v3::Cluster::RingHashLbConfig();
    config_.value().mutable_minimum_ring_size()->set_value(min_ring_size);
    ring_hash_lb_ = std::make_unique<RingHashLoadBalancer>(
        priority_set_, stats_, stats_store_, runtime_, random_, config_, common_config_);
  }

  absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig> config_;
  std::unique_ptr<RingHashLoadBalancer> ring_hash_lb_;
};

class MaglevTester : public BaseTester {
public:
  MaglevTester(uint64_t num_hosts, uint32_t weighted_subset_percent = 0, uint32_t weight = 0)
      : BaseTester(num_hosts, weighted_subset_percent, weight) {
    maglev_lb_ = std::make_unique<MaglevLoadBalancer>(priority_set_, stats_, stats_store_, runtime_,
                                                      random_, common_config_);
  }

  std::unique_ptr<MaglevLoadBalancer> maglev_lb_;
};

uint64_t hashInt(uint64_t i) {
  // Hack to hash an integer.
  return HashUtil::xxHash64(absl::string_view(reinterpret_cast<const char*>(&i), sizeof(i)));
}

void BM_RingHashLoadBalancerBuildRing(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t min_ring_size = state.range(1);
    RingHashTester tester(num_hosts, min_ring_size);

    const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();

    // We are only interested in timing the initial ring build.
    state.ResumeTiming();
    tester.ring_hash_lb_->initialize();
    state.PauseTiming();
    const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
    state.counters["memory"] = end_mem - start_mem;
    state.counters["memory_per_host"] = (end_mem - start_mem) / num_hosts;
    state.ResumeTiming();
  }
}
BENCHMARK(BM_RingHashLoadBalancerBuildRing)
    ->Args({100, 65536})
    ->Args({200, 65536})
    ->Args({500, 65536})
    ->Args({100, 256000})
    ->Args({200, 256000})
    ->Args({500, 256000})
    ->Unit(benchmark::kMillisecond);

void BM_MaglevLoadBalancerBuildTable(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    MaglevTester tester(num_hosts);

    const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();

    // We are only interested in timing the initial table build.
    state.ResumeTiming();
    tester.maglev_lb_->initialize();
    state.PauseTiming();
    const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
    state.counters["memory"] = end_mem - start_mem;
    state.counters["memory_per_host"] = (end_mem - start_mem) / num_hosts;
    state.ResumeTiming();
  }
}
BENCHMARK(BM_MaglevLoadBalancerBuildTable)
    ->Arg(100)
    ->Arg(200)
    ->Arg(500)
    ->Unit(benchmark::kMillisecond);

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  absl::optional<uint64_t> hash_key_;
};

void computeHitStats(benchmark::State& state,
                     const std::unordered_map<std::string, uint64_t>& hit_counter) {
  double mean = 0;
  for (const auto& pair : hit_counter) {
    mean += pair.second;
  }
  mean /= hit_counter.size();

  double variance = 0;
  for (const auto& pair : hit_counter) {
    variance += std::pow(pair.second - mean, 2);
  }
  variance /= hit_counter.size();
  const double stddev = std::sqrt(variance);

  state.counters["mean_hits"] = mean;
  state.counters["stddev_hits"] = stddev;
  state.counters["relative_stddev_hits"] = (stddev / mean);
}

void BM_LeastRequestLoadBalancerChooseHost(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t choice_count = state.range(1);
    const uint64_t keys_to_simulate = state.range(2);
    LeastRequestTester tester(num_hosts, choice_count);
    std::unordered_map<std::string, uint64_t> hit_counter;
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
BENCHMARK(BM_LeastRequestLoadBalancerChooseHost)
    ->Args({100, 1, 1000000})
    ->Args({100, 2, 1000000})
    ->Args({100, 3, 1000000})
    ->Args({100, 10, 1000000})
    ->Args({100, 50, 1000000})
    ->Args({100, 100, 1000000})
    ->Unit(benchmark::kMillisecond);

void BM_RingHashLoadBalancerChooseHost(benchmark::State& state) {
  for (auto _ : state) {
    // Do not time the creation of the ring.
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t min_ring_size = state.range(1);
    const uint64_t keys_to_simulate = state.range(2);
    RingHashTester tester(num_hosts, min_ring_size);
    tester.ring_hash_lb_->initialize();
    LoadBalancerPtr lb = tester.ring_hash_lb_->factory()->create();
    std::unordered_map<std::string, uint64_t> hit_counter;
    TestLoadBalancerContext context;
    state.ResumeTiming();

    // Note: To a certain extent this is benchmarking the performance of xxhash as well as
    // std::unordered_map. However, it should be roughly equivalent to the work done when
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
BENCHMARK(BM_RingHashLoadBalancerChooseHost)
    ->Args({100, 65536, 100000})
    ->Args({200, 65536, 100000})
    ->Args({500, 65536, 100000})
    ->Args({100, 256000, 100000})
    ->Args({200, 256000, 100000})
    ->Args({500, 256000, 100000})
    ->Unit(benchmark::kMillisecond);

void BM_MaglevLoadBalancerChooseHost(benchmark::State& state) {
  for (auto _ : state) {
    // Do not time the creation of the table.
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t keys_to_simulate = state.range(1);
    MaglevTester tester(num_hosts);
    tester.maglev_lb_->initialize();
    LoadBalancerPtr lb = tester.maglev_lb_->factory()->create();
    std::unordered_map<std::string, uint64_t> hit_counter;
    TestLoadBalancerContext context;
    state.ResumeTiming();

    // Note: To a certain extent this is benchmarking the performance of xxhash as well as
    // std::unordered_map. However, it should be roughly equivalent to the work done when
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
BENCHMARK(BM_MaglevLoadBalancerChooseHost)
    ->Args({100, 100000})
    ->Args({200, 100000})
    ->Args({500, 100000})
    ->Unit(benchmark::kMillisecond);

void BM_RingHashLoadBalancerHostLoss(benchmark::State& state) {
  for (auto _ : state) {
    const uint64_t num_hosts = state.range(0);
    const uint64_t min_ring_size = state.range(1);
    const uint64_t hosts_to_lose = state.range(2);
    const uint64_t keys_to_simulate = state.range(3);

    RingHashTester tester(num_hosts, min_ring_size);
    tester.ring_hash_lb_->initialize();
    LoadBalancerPtr lb = tester.ring_hash_lb_->factory()->create();
    std::vector<HostConstSharedPtr> hosts;
    TestLoadBalancerContext context;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts.push_back(lb->chooseHost(&context));
    }

    RingHashTester tester2(num_hosts - hosts_to_lose, min_ring_size);
    tester2.ring_hash_lb_->initialize();
    lb = tester2.ring_hash_lb_->factory()->create();
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
BENCHMARK(BM_RingHashLoadBalancerHostLoss)
    ->Args({500, 256000, 1, 10000})
    ->Args({500, 256000, 2, 10000})
    ->Args({500, 256000, 3, 10000})
    ->Unit(benchmark::kMillisecond);

void BM_MaglevLoadBalancerHostLoss(benchmark::State& state) {
  for (auto _ : state) {
    const uint64_t num_hosts = state.range(0);
    const uint64_t hosts_to_lose = state.range(1);
    const uint64_t keys_to_simulate = state.range(2);

    MaglevTester tester(num_hosts);
    tester.maglev_lb_->initialize();
    LoadBalancerPtr lb = tester.maglev_lb_->factory()->create();
    std::vector<HostConstSharedPtr> hosts;
    TestLoadBalancerContext context;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts.push_back(lb->chooseHost(&context));
    }

    MaglevTester tester2(num_hosts - hosts_to_lose);
    tester2.maglev_lb_->initialize();
    lb = tester2.maglev_lb_->factory()->create();
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
BENCHMARK(BM_MaglevLoadBalancerHostLoss)
    ->Args({500, 1, 10000})
    ->Args({500, 2, 10000})
    ->Args({500, 3, 10000})
    ->Unit(benchmark::kMillisecond);

void BM_MaglevLoadBalancerWeighted(benchmark::State& state) {
  for (auto _ : state) {
    const uint64_t num_hosts = state.range(0);
    const uint64_t weighted_subset_percent = state.range(1);
    const uint64_t before_weight = state.range(2);
    const uint64_t after_weight = state.range(3);
    const uint64_t keys_to_simulate = state.range(4);

    MaglevTester tester(num_hosts, weighted_subset_percent, before_weight);
    tester.maglev_lb_->initialize();
    LoadBalancerPtr lb = tester.maglev_lb_->factory()->create();
    std::vector<HostConstSharedPtr> hosts;
    TestLoadBalancerContext context;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = hashInt(i);
      hosts.push_back(lb->chooseHost(&context));
    }

    MaglevTester tester2(num_hosts, weighted_subset_percent, after_weight);
    tester2.maglev_lb_->initialize();
    lb = tester2.maglev_lb_->factory()->create();
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
BENCHMARK(BM_MaglevLoadBalancerWeighted)
    ->Args({500, 5, 1, 1, 10000})
    ->Args({500, 5, 1, 127, 1000})
    ->Args({500, 5, 127, 1, 10000})
    ->Args({500, 50, 1, 127, 1000})
    ->Args({500, 50, 127, 1, 10000})
    ->Args({500, 95, 1, 127, 1000})
    ->Args({500, 95, 127, 1, 10000})
    ->Args({500, 95, 25, 75, 1000})
    ->Args({500, 95, 75, 25, 10000})
    ->Unit(benchmark::kMillisecond);

} // namespace
} // namespace Upstream
} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  // TODO(mattklein123): Provide a common bazel benchmark wrapper much like we do for normal tests,
  // fuzz, etc.
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logging_context(spdlog::level::warn,
                                         Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
