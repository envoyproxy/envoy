// Usage: bazel run //test/common/upstream:load_balancer_benchmark

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/random_generator.h"
#include "source/common/memory/stats.h"
#include "source/common/upstream/maglev_lb.h"
#include "source/common/upstream/ring_hash_lb.h"
#include "source/common/upstream/subset_lb.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/benchmark/main.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Upstream {
namespace {

class BaseTester : public Event::TestUsingSimulatedTime {
public:
  static constexpr absl::string_view metadata_key = "key";
  // We weight the first weighted_subset_percent of hosts with weight.
  BaseTester(uint64_t num_hosts, uint32_t weighted_subset_percent = 0, uint32_t weight = 0,
             bool attach_metadata = false) {
    HostVector hosts;
    ASSERT(num_hosts < 65536);
    for (uint64_t i = 0; i < num_hosts; i++) {
      const bool should_weight = i < num_hosts * (weighted_subset_percent / 100.0);
      const std::string url = fmt::format("tcp://10.0.{}.{}:6379", i / 256, i % 256);
      const auto effective_weight = should_weight ? weight : 1;
      if (attach_metadata) {
        envoy::config::core::v3::Metadata metadata;
        ProtobufWkt::Value value;
        value.set_number_value(i);
        ProtobufWkt::Struct& map =
            (*metadata.mutable_filter_metadata())[Config::MetadataFilters::get().ENVOY_LB];
        (*map.mutable_fields())[std::string(metadata_key)] = value;

        hosts.push_back(makeTestHost(info_, url, metadata, simTime(), effective_weight));
      } else {
        hosts.push_back(makeTestHost(info_, url, simTime(), effective_weight));
      }
    }

    HostVectorConstSharedPtr updated_hosts = std::make_shared<HostVector>(hosts);
    HostsPerLocalityConstSharedPtr hosts_per_locality = makeHostsPerLocality({hosts});
    priority_set_.updateHosts(0, HostSetImpl::partitionHosts(updated_hosts, hosts_per_locality), {},
                              hosts, {}, absl::nullopt);
    local_priority_set_.updateHosts(0,
                                    HostSetImpl::partitionHosts(updated_hosts, hosts_per_locality),
                                    {}, hosts, {}, absl::nullopt);
  }

  Envoy::Thread::MutexBasicLockable lock_;
  // Reduce default log level to warn while running this benchmark to avoid problems due to
  // excessive debug logging in upstream_impl.cc
  Envoy::Logger::Context logging_context_{spdlog::level::warn,
                                          Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock_, false};

  PrioritySetImpl priority_set_;
  PrioritySetImpl local_priority_set_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterLbStatNames stat_names_{stats_store_.symbolTable()};
  ClusterLbStats stats_{stat_names_, stats_store_};
  NiceMock<Runtime::MockLoader> runtime_;
  Random::RandomGeneratorImpl random_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  envoy::config::cluster::v3::Cluster::RoundRobinLbConfig round_robin_lb_config_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
};

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
                                                      random_, config_, common_config_);
  }

  absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig> config_;
  std::unique_ptr<MaglevLoadBalancer> maglev_lb_;
};

uint64_t hashInt(uint64_t i) {
  // Hack to hash an integer.
  return HashUtil::xxHash64(absl::string_view(reinterpret_cast<const char*>(&i), sizeof(i)));
}

void benchmarkRingHashLoadBalancerBuildRing(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
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
BENCHMARK(benchmarkRingHashLoadBalancerBuildRing)
    ->Args({100, 65536})
    ->Args({200, 65536})
    ->Args({500, 65536})
    ->Args({100, 256000})
    ->Args({200, 256000})
    ->Args({500, 256000})
    ->Unit(::benchmark::kMillisecond);

void benchmarkMaglevLoadBalancerBuildTable(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
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
BENCHMARK(benchmarkMaglevLoadBalancerBuildTable)
    ->Arg(100)
    ->Arg(200)
    ->Arg(500)
    ->Unit(::benchmark::kMillisecond);

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  absl::optional<uint64_t> hash_key_;
};

void computeHitStats(::benchmark::State& state,
                     const absl::node_hash_map<std::string, uint64_t>& hit_counter) {
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

void benchmarkRingHashLoadBalancerChooseHost(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    // Do not time the creation of the ring.
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t min_ring_size = state.range(1);
    const uint64_t keys_to_simulate = state.range(2);
    RingHashTester tester(num_hosts, min_ring_size);
    tester.ring_hash_lb_->initialize();
    LoadBalancerPtr lb = tester.ring_hash_lb_->factory()->create();
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

void benchmarkMaglevLoadBalancerChooseHost(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    // Do not time the creation of the table.
    state.PauseTiming();
    const uint64_t num_hosts = state.range(0);
    const uint64_t keys_to_simulate = state.range(1);
    MaglevTester tester(num_hosts);
    tester.maglev_lb_->initialize();
    LoadBalancerPtr lb = tester.maglev_lb_->factory()->create();
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
BENCHMARK(benchmarkRingHashLoadBalancerHostLoss)
    ->Args({500, 65536, 1, 10000})
    ->Args({500, 65536, 2, 10000})
    ->Args({500, 65536, 3, 10000})
    ->Args({500, 256000, 1, 10000})
    ->Args({500, 256000, 2, 10000})
    ->Args({500, 256000, 3, 10000})
    ->Unit(::benchmark::kMillisecond);

void benchmarkMaglevLoadBalancerHostLoss(::benchmark::State& state) {
  for (auto _ : state) { // NOLINT: Silences warning about dead store
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

class SubsetLbTester : public BaseTester {
public:
  SubsetLbTester(uint64_t num_hosts, bool single_host_per_subset)
      : BaseTester(num_hosts, 0, 0, true /* attach metadata */) {
    envoy::config::cluster::v3::Cluster::LbSubsetConfig subset_config;
    subset_config.set_fallback_policy(
        envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT);
    auto* selector = subset_config.mutable_subset_selectors()->Add();
    selector->set_single_host_per_subset(single_host_per_subset);
    *selector->mutable_keys()->Add() = std::string(metadata_key);

    subset_info_ = std::make_unique<LoadBalancerSubsetInfoImpl>(subset_config);
    lb_ = std::make_unique<SubsetLoadBalancer>(
        LoadBalancerType::Random, priority_set_, &local_priority_set_, stats_, stats_store_,
        runtime_, random_, *subset_info_, absl::nullopt, absl::nullopt, absl::nullopt,
        absl::nullopt, common_config_, simTime());

    const HostVector& hosts = priority_set_.getOrCreateHostSet(0).hosts();
    ASSERT(hosts.size() == num_hosts);
    orig_hosts_ = std::make_shared<HostVector>(hosts);
    smaller_hosts_ = std::make_shared<HostVector>(hosts.begin() + 1, hosts.end());
    ASSERT(smaller_hosts_->size() + 1 == orig_hosts_->size());
    orig_locality_hosts_ = makeHostsPerLocality({*orig_hosts_});
    smaller_locality_hosts_ = makeHostsPerLocality({*smaller_hosts_});
  }

  // Remove a host and add it back.
  void update() {
    priority_set_.updateHosts(0,
                              HostSetImpl::partitionHosts(smaller_hosts_, smaller_locality_hosts_),
                              nullptr, {}, host_moved_, absl::nullopt);
    priority_set_.updateHosts(0, HostSetImpl::partitionHosts(orig_hosts_, orig_locality_hosts_),
                              nullptr, host_moved_, {}, absl::nullopt);
  }

  std::unique_ptr<LoadBalancerSubsetInfoImpl> subset_info_;
  std::unique_ptr<SubsetLoadBalancer> lb_;
  HostVectorConstSharedPtr orig_hosts_;
  HostVectorConstSharedPtr smaller_hosts_;
  HostsPerLocalitySharedPtr orig_locality_hosts_;
  HostsPerLocalitySharedPtr smaller_locality_hosts_;
  HostVector host_moved_;
};

void benchmarkSubsetLoadBalancerCreate(::benchmark::State& state) {
  const bool single_host_per_subset = state.range(0);
  const uint64_t num_hosts = state.range(1);

  if (benchmark::skipExpensiveBenchmarks() && num_hosts > 100) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SubsetLbTester tester(num_hosts, single_host_per_subset);
  }
}

BENCHMARK(benchmarkSubsetLoadBalancerCreate)
    ->Ranges({{false, true}, {50, 2500}})
    ->Unit(::benchmark::kMillisecond);

void benchmarkSubsetLoadBalancerUpdate(::benchmark::State& state) {
  const bool single_host_per_subset = state.range(0);
  const uint64_t num_hosts = state.range(1);
  if (benchmark::skipExpensiveBenchmarks() && num_hosts > 100) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  SubsetLbTester tester(num_hosts, single_host_per_subset);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    tester.update();
  }
}

BENCHMARK(benchmarkSubsetLoadBalancerUpdate)
    ->Ranges({{false, true}, {50, 2500}})
    ->Unit(::benchmark::kMillisecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
