#include "common/runtime/runtime_impl.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

#include "testing/base/public/benchmark.h"

namespace Envoy {
namespace Upstream {

class RingHashTester {
public:
  RingHashTester(uint64_t num_hosts, uint64_t min_ring_size) {
    HostSet& host_set = priority_set_.getOrCreateHostSet(0);

    HostVector hosts;
    ASSERT(num_hosts < 65536);
    for (uint64_t i = 0; i < num_hosts; i++) {
      hosts.push_back(makeTestHost(info_, fmt::format("tcp://10.0.{}.{}:6379", i / 256, i % 256)));
    }
    HostVectorConstSharedPtr updated_hosts{new HostVector(hosts)};
    host_set.updateHosts(updated_hosts, updated_hosts, nullptr, nullptr, hosts, {});

    config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
    config_.value().mutable_minimum_ring_size()->set_value(min_ring_size);
    ring_hash_lb_.reset(
        new RingHashLoadBalancer{priority_set_, stats_, runtime_, random_, config_});
  }

  PrioritySetImpl priority_set_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_{ClusterInfoImpl::generateStats(stats_store_)};
  NiceMock<Runtime::MockLoader> runtime_;
  Runtime::RandomGeneratorImpl random_;
  Optional<envoy::api::v2::Cluster::RingHashLbConfig> config_;
  std::unique_ptr<RingHashLoadBalancer> ring_hash_lb_;
};

static void BM_RingHashLoadBalancerBuildRing(benchmark::State& state) {
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

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  // Upstream::LoadBalancerContext
  Optional<uint64_t> computeHashKey() override { return hash_key_; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
  const Network::Connection* downstreamConnection() const override { return nullptr; }

  Optional<uint64_t> hash_key_;
};

static void BM_RingHashLoadBalancerChooseHost(benchmark::State& state) {
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
    // comparing to different hashing algorithms such as Maglev.
    for (uint64_t i = 0; i < keys_to_simulate; i++) {
      context.hash_key_.value(
          HashUtil::xxHash64(absl::string_view(reinterpret_cast<const char*>(&i), sizeof(i))));
      hit_counter[lb->chooseHost(&context)->address()->asString()] += 1;
    }

    // Do not time computation of mean, standard deviation, and relative standard deviation.
    state.PauseTiming();
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

} // namespace Upstream
} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  // TODO(mattklein123): Provide a common bazel benchmark wrapper much like we do for normal tests,
  // fuzz, etc.
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Registry::initialize(spdlog::level::warn, lock);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
