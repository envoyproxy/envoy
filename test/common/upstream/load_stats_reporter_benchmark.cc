#include "envoy/common/optref.h"
#include "envoy/event/timer.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "source/common/network/address_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/upstream/load_stats_reporter_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/benchmark/main.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
namespace {

inline HostsPerLocalitySharedPtr makeHostsPerLocality(std::vector<HostVector>&& locality_hosts,
                                                      bool force_no_local_locality = false) {
  return std::make_shared<HostsPerLocalityImpl>(
      std::move(locality_hosts), !force_no_local_locality && !locality_hosts.empty());
}

HostSharedPtr makeTestHost(const std::string& hostname,
                           const ::envoy::config::core::v3::Locality& locality) {
  const auto host = std::make_shared<NiceMock<::Envoy::Upstream::MockHost>>();
  ON_CALL(*host, hostname()).WillByDefault(::testing::ReturnRef(hostname));
  ON_CALL(*host, locality()).WillByDefault(::testing::ReturnRef(locality));

  auto address = std::make_shared<Envoy::Network::Address::Ipv4Instance>("127.0.0.1", 80);
  ON_CALL(*host, address()).WillByDefault(::testing::Return(address));
  return host;
}

class BenchmarkClusterInfo : public NiceMock<MockClusterInfo> {
public:
  BenchmarkClusterInfo(const std::string& name) : name_(name) {}
  const std::string& name() const override { return name_; }

private:
  const std::string name_;
};

class BenchmarkCluster : public NiceMock<MockClusterMockPrioritySet> {
public:
  BenchmarkCluster(const std::string& name) {
    info_ = std::make_shared<BenchmarkClusterInfo>(name);
  }
  ClusterInfoConstSharedPtr info() const override { return info_; }
};

class CustomMockClusterManager : public NiceMock<MockClusterManager> {
public:
  void addCluster(const std::string& name, std::shared_ptr<BenchmarkCluster> cluster) {
    active_clusters_map_[name] = cluster;
    active_clusters_vec_.push_back(cluster);
  }

  ClusterInfoMaps clusters() const override {
    ClusterInfoMaps maps;
    for (const auto& p : active_clusters_map_) {
      maps.active_clusters_.emplace(p.first, *p.second);
    }
    return maps;
  }

  void forEachActiveCluster(std::function<void(const Cluster&)> cb) const override {
    for (const auto& cluster : active_clusters_vec_) {
      cb(*cluster);
    }
  }

  OptRef<const Cluster> getActiveCluster(const std::string& cluster_name) const override {
    auto it = active_clusters_map_.find(cluster_name);
    if (it != active_clusters_map_.end()) {
      return *it->second;
    }
    return absl::nullopt;
  }

private:
  absl::flat_hash_map<std::string, std::shared_ptr<BenchmarkCluster>> active_clusters_map_;
  std::vector<std::shared_ptr<BenchmarkCluster>> active_clusters_vec_;
};

class TestLoadStatsReporterImpl : public LoadStatsReporterImpl {
public:
  using LoadStatsReporterImpl::LoadStatsReporterImpl;
  void triggerSendLoadStatsRequest() { sendLoadStatsRequest(); }
};

void bmStartLoadReportPeriod(::benchmark::State& state, bool use_new_path, bool send_all_clusters) {
  const int num_clusters = state.range(0);
  Event::SimulatedTimeSystem time_system;

  if (benchmark::skipExpensiveBenchmarks() && num_clusters > 64) {
    state.SkipWithError("Skipping expensive benchmark in test mode");
    return;
  }

  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.optimized_lrs_enabled", use_new_path ? "true" : "false"}});

  NiceMock<LocalInfo::MockLocalInfo> local_info;
  CustomMockClusterManager cm;
  Stats::IsolatedStoreImpl stats_store;
  auto async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  NiceMock<Event::MockDispatcher> dispatcher;

  for (int i = 0; i < num_clusters; i++) {
    auto cluster = std::make_shared<BenchmarkCluster>(absl::StrCat("cluster_", i));
    cm.addCluster(absl::StrCat("cluster_", i), cluster);
  }

  ON_CALL(dispatcher, createTimer_(_)).WillByDefault(Invoke([](Event::TimerCb) {
    return new NiceMock<Event::MockTimer>();
  }));

  auto async_stream = std::make_shared<NiceMock<Grpc::MockAsyncStream>>();
  ON_CALL(*async_client, startRaw(_, _, _, _)).WillByDefault(Return(async_stream.get()));

  LoadStatsReporterImpl reporter(local_info, cm, *stats_store.rootScope(), async_client,
                                 dispatcher);

  auto message = std::make_unique<envoy::service::load_stats::v3::LoadStatsResponse>();
  message->set_send_all_clusters(send_all_clusters);
  if (!send_all_clusters) {
    for (int i = 0; i < num_clusters; i += 3) {
      message->add_clusters(absl::StrCat("cluster_", i));
    }
  }
  auto warmup_msg = std::make_unique<envoy::service::load_stats::v3::LoadStatsResponse>();
  warmup_msg->CopyFrom(*message);
  reporter.onReceiveMessage(std::move(warmup_msg));

  for (auto _ : state) { // NOLINT
    state.PauseTiming();
    auto msg = std::make_unique<envoy::service::load_stats::v3::LoadStatsResponse>();
    msg->CopyFrom(*message);
    state.ResumeTiming();

    reporter.onReceiveMessage(std::move(msg));
  }
}

void bmLrsFlush(::benchmark::State& state, bool use_new_path, double active_cluster_ratio) {
  const int num_clusters = state.range(0);
  const int hosts_per_cluster = state.range(1);

  if (benchmark::skipExpensiveBenchmarks() && (num_clusters > 64 || hosts_per_cluster > 10)) {
    state.SkipWithError("Skipping expensive benchmark in test mode");
    return;
  }

  Event::SimulatedTimeSystem time_system;

  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.optimized_lrs_enabled", use_new_path ? "true" : "false"}});

  NiceMock<LocalInfo::MockLocalInfo> local_info;
  CustomMockClusterManager cm;
  Stats::IsolatedStoreImpl stats_store;
  auto async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  NiceMock<Event::MockDispatcher> dispatcher;

  ON_CALL(dispatcher, createTimer_(_)).WillByDefault(Invoke([](Event::TimerCb) {
    return new NiceMock<Event::MockTimer>();
  }));

  auto async_stream = std::make_shared<NiceMock<Grpc::MockAsyncStream>>();
  ON_CALL(*async_client, startRaw(_, _, _, _)).WillByDefault(Return(async_stream.get()));

  std::vector<std::shared_ptr<BenchmarkCluster>> clusters;
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  for (int i = 0; i < num_clusters; i++) {
    auto cluster = std::make_shared<BenchmarkCluster>(absl::StrCat("cluster_", i));
    cm.addCluster(absl::StrCat("cluster_", i), cluster);
    clusters.push_back(cluster);

    MockHostSet& host_set = *cluster->prioritySet().getMockHostSet(0);
    std::vector<HostSharedPtr> hosts;
    for (int j = 0; j < hosts_per_cluster; j++) {
      hosts.push_back(makeTestHost(absl::StrCat("host_", j), locality));
    }
    host_set.hosts_per_locality_ = makeHostsPerLocality({hosts});
  }

  TestLoadStatsReporterImpl reporter(local_info, cm, *stats_store.rootScope(), async_client,
                                     dispatcher);

  auto message = std::make_unique<envoy::service::load_stats::v3::LoadStatsResponse>();
  message->set_send_all_clusters(true);
  message->mutable_load_reporting_interval()->set_seconds(42);
  reporter.onReceiveMessage(std::move(message));

  const int active_clusters_count = num_clusters * active_cluster_ratio;

  for (auto _ : state) { // NOLINT
    state.PauseTiming();
    // Replenish stats for active clusters and hosts
    for (int i = 0; i < active_clusters_count; i++) {
      auto& cluster = *clusters[i];
      cluster.info_->load_report_stats_.upstream_rq_total_.inc();

      MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
      const HostVector& hosts = host_set.hostsPerLocality().get()[0];
      for (int j = 0; j < hosts_per_cluster; j++) {
        hosts[j]->stats().rq_total_.inc();
        hosts[j]->stats().rq_success_.inc();
      }
    }
    state.ResumeTiming();

    reporter.triggerSendLoadStatsRequest();
  }
}

// Benchmarks for startLoadReportPeriod
BENCHMARK_CAPTURE(bmStartLoadReportPeriod, old_path_send_all, false, true)->Range(10, 512);
BENCHMARK_CAPTURE(bmStartLoadReportPeriod, new_path_send_all, true, true)->Range(10, 512);
BENCHMARK_CAPTURE(bmStartLoadReportPeriod, old_path_specific, false, false)->Range(10, 512);
BENCHMARK_CAPTURE(bmStartLoadReportPeriod, new_path_specific, true, false)->Range(10, 512);

// Benchmarks for sendLoadStatsRequest (bmLrsFlush)
// Scenario 1: Idle Mesh
BENCHMARK_CAPTURE(bmLrsFlush, IdleMesh_OldPath, false, 0.0)->Ranges({{10, 256}, {10, 32}});
BENCHMARK_CAPTURE(bmLrsFlush, IdleMesh_NewPath, true, 0.0)->Ranges({{10, 256}, {10, 32}});

// Scenario 2: Sparse Clusters
BENCHMARK_CAPTURE(bmLrsFlush, SparseClusters_OldPath, false, 0.1)->Ranges({{10, 256}, {10, 32}});
BENCHMARK_CAPTURE(bmLrsFlush, SparseClusters_NewPath, true, 0.1)->Ranges({{10, 256}, {10, 32}});

// Scenario 3: Moderate Clusters Active
BENCHMARK_CAPTURE(bmLrsFlush, ModerateClusters_OldPath, false, 1.0)->Ranges({{10, 256}, {10, 32}});
BENCHMARK_CAPTURE(bmLrsFlush, ModerateClusters_NewPath, true, 1.0)->Ranges({{10, 256}, {10, 32}});

// Scenario 6: Fully Active
BENCHMARK_CAPTURE(bmLrsFlush, FullyActive_OldPath, false, 1.0)->Ranges({{10, 256}, {10, 32}});
BENCHMARK_CAPTURE(bmLrsFlush, FullyActive_NewPath, true, 1.0)->Ranges({{10, 256}, {10, 32}});

} // namespace
} // namespace Upstream
} // namespace Envoy
