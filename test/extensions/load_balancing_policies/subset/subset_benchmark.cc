// Usage: bazel run //test/common/upstream:load_balancer_benchmark

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/random_generator.h"
#include "source/common/memory/stats.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/maglev/maglev_lb.h"
#include "source/extensions/load_balancing_policies/ring_hash/ring_hash_lb.h"
#include "source/extensions/load_balancing_policies/subset/subset_lb.h"

#include "test/benchmark/main.h"
#include "test/common/upstream/utility.h"
#include "test/extensions/load_balancing_policies/common/benchmark_base_tester.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/types/optional.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Subset {
namespace {

class SubsetLbTester : public LoadBalancingPolices::Common::BaseTester {
public:
  SubsetLbTester(uint64_t num_hosts, bool single_host_per_subset)
      : BaseTester(num_hosts, 0, 0, true /* attach metadata */) {
    envoy::config::cluster::v3::Cluster::LbSubsetConfig subset_config;
    subset_config.set_fallback_policy(
        envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT);
    auto* selector = subset_config.mutable_subset_selectors()->Add();
    selector->set_single_host_per_subset(single_host_per_subset);
    *selector->mutable_keys()->Add() = std::string(metadata_key);

    subset_info_ = std::make_unique<Upstream::LoadBalancerSubsetInfoImpl>(subset_config);
    auto child_lb_creator = std::make_unique<Upstream::LegacyChildLoadBalancerCreatorImpl>(
        Upstream::LoadBalancerType::Random, absl::nullopt, absl::nullopt, absl::nullopt,
        absl::nullopt, common_config_);
    lb_ = std::make_unique<Upstream::SubsetLoadBalancer>(
        *subset_info_, std::move(child_lb_creator), priority_set_, &local_priority_set_, stats_,
        stats_scope_, runtime_, random_, simTime());

    const Upstream::HostVector& hosts = priority_set_.getOrCreateHostSet(0).hosts();
    ASSERT(hosts.size() == num_hosts);
    orig_hosts_ = std::make_shared<Upstream::HostVector>(hosts);
    smaller_hosts_ = std::make_shared<Upstream::HostVector>(hosts.begin() + 1, hosts.end());
    ASSERT(smaller_hosts_->size() + 1 == orig_hosts_->size());
    orig_locality_hosts_ = Upstream::makeHostsPerLocality({*orig_hosts_});
    smaller_locality_hosts_ = Upstream::makeHostsPerLocality({*smaller_hosts_});
  }

  // Remove a host and add it back.
  void update() {
    priority_set_.updateHosts(
        0, Upstream::HostSetImpl::partitionHosts(smaller_hosts_, smaller_locality_hosts_), nullptr,
        {}, host_moved_, absl::nullopt);
    priority_set_.updateHosts(
        0, Upstream::HostSetImpl::partitionHosts(orig_hosts_, orig_locality_hosts_), nullptr,
        host_moved_, {}, absl::nullopt);
  }

  std::unique_ptr<Upstream::LoadBalancerSubsetInfoImpl> subset_info_;
  std::unique_ptr<Upstream::SubsetLoadBalancer> lb_;
  Upstream::HostVectorConstSharedPtr orig_hosts_;
  Upstream::HostVectorConstSharedPtr smaller_hosts_;
  Upstream::HostsPerLocalitySharedPtr orig_locality_hosts_;
  Upstream::HostsPerLocalitySharedPtr smaller_locality_hosts_;
  Upstream::HostVector host_moved_;
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
} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
