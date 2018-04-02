#include "common/runtime/runtime_impl.h"
#include "common/upstream/maglev_lb.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

#include "testing/base/public/benchmark.h"

namespace Envoy {
namespace Upstream {
namespace {

class BaseTester {
public:
  BaseTester(uint64_t num_hosts) {
    HostSet& host_set = priority_set_.getOrCreateHostSet(0);

    HostVector hosts;
    ASSERT(num_hosts < 65536);
    for (uint64_t i = 0; i < num_hosts; i++) {
      hosts.push_back(makeTestHost(info_, fmt::format("tcp://10.0.{}.{}:6379", i / 256, i % 256)));
    }
    HostVectorConstSharedPtr updated_hosts{new HostVector(hosts)};
    host_set.updateHosts(updated_hosts, updated_hosts, nullptr, nullptr, {}, hosts, {});
  }

  PrioritySetImpl priority_set_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
};

class RingHashTester : public BaseTester {
public:
  RingHashTester(uint64_t num_hosts, uint64_t min_ring_size) : BaseTester(num_hosts) {
    config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
    config_.value().mutable_minimum_ring_size()->set_value(min_ring_size);
    ring_hash_lb_.reset(new RingHashLoadBalancer{priority_set_, stats_, runtime_, random_, config_,
                                                 common_config_});
  }

  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_{ClusterInfoImpl::generateStats(stats_store_)};
  NiceMock<Runtime::MockLoader> runtime_;
  Runtime::RandomGeneratorImpl random_;
  absl::optional<envoy::api::v2::Cluster::RingHashLbConfig> config_;
  std::unique_ptr<RingHashLoadBalancer> ring_hash_lb_;
  envoy::api::v2::Cluster::CommonLbConfig common_config_;
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
    state.ResumeTiming();

    // We are only interested in timing the initial ring build.
    tester.ring_hash_lb_->initialize();
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
    BaseTester tester(num_hosts);
    state.ResumeTiming();
    MaglevTable table(tester.priority_set_.getOrCreateHostSet(0).hosts());
  }
}
BENCHMARK(BM_MaglevLoadBalancerBuildTable)
    ->Arg(100)
    ->Arg(200)
    ->Arg(500)
    ->Unit(benchmark::kMillisecond);

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
  const Network::Connection* downstreamConnection() const override { return nullptr; }

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
      context.hash_key_ = (hashInt(i));
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
    BaseTester tester(num_hosts);
    MaglevTable table(tester.priority_set_.getOrCreateHostSet(0).hosts());
    std::unordered_map<std::string, uint64_t> hit_counter;
    state.ResumeTiming();

    // Note: To a certain extent this is benchmarking the performance of xxhash as well as
    // std::unordered_map. However, it should be roughly equivalent to the work done when
    // comparing different hashing algorithms.
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      hit_counter[table.chooseHost(hashInt(i))->address()->asString()] += 1;
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
      context.hash_key_ = (hashInt(i));
      hosts.push_back(lb->chooseHost(&context));
    }

    RingHashTester tester2(num_hosts - hosts_to_lose, min_ring_size);
    tester2.ring_hash_lb_->initialize();
    lb = tester2.ring_hash_lb_->factory()->create();
    std::vector<HostConstSharedPtr> hosts2;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_ = (hashInt(i));
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

    BaseTester tester(num_hosts);
    MaglevTable table(tester.priority_set_.getOrCreateHostSet(0).hosts());
    std::vector<HostConstSharedPtr> hosts;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      hosts.push_back(table.chooseHost(hashInt(i)));
    }

    BaseTester tester2(num_hosts - hosts_to_lose);
    MaglevTable table2(tester2.priority_set_.getOrCreateHostSet(0).hosts());
    std::vector<HostConstSharedPtr> hosts2;
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      hosts2.push_back(table2.chooseHost(hashInt(i)));
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

} // namespace
} // namespace Upstream
} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  // TODO(mattklein123): Provide a common bazel benchmark wrapper much like we do for normal tests,
  // fuzz, etc.
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Registry::initialize(spdlog::level::warn,
                                      Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
