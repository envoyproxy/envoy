// Benchmarks for LoadStatsReporterImpl: the cost of starting a load report period (tracking the
// selected clusters) and of sending a load report, for various numbers of clusters and hosts and
// shares of clusters with load to report.
//
// Run manually with e.g.
//   bazel run -c opt //test/common/upstream:load_stats_reporter_benchmark \
//     -- --benchmark_filter=SendLoadStatsRequest
//
// The runtime feature envoy.reloadable_features.lrs_skip_idle_clusters controls whether the hosts
// of clusters without load to report are visited.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/timer.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/common/upstream/load_stats_reporter_impl.h"

#include "test/benchmark/main.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/host_set.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

#include "absl/container/btree_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

class BenchmarkClusterInfo : public NiceMock<MockClusterInfo> {
public:
  explicit BenchmarkClusterInfo(const std::string& name) { name_ = name; }

  // The reporter looks clusters up by name on every report. Bypass gmock for it, whose per-call
  // overhead (hundreds of nanoseconds) would otherwise dominate the per-cluster cost; in
  // production ClusterInfoImpl::name() is a plain accessor. The other mocked accessors on the
  // per-cluster path (edsServiceName(), loadReportStats(), prioritySet() and the host set) still go
  // through gmock, which inflates the per-cluster floor of all variants equally.
  const std::string& name() const override { return name_; }
};

class BenchmarkCluster : public NiceMock<MockClusterMockPrioritySet> {
public:
  explicit BenchmarkCluster(const std::string& name) {
    info_ = std::make_shared<BenchmarkClusterInfo>(name);
  }
  ClusterInfoConstSharedPtr info() const override { return info_; }
};

// A cluster manager that, like ClusterManagerImpl, keeps its active clusters in a btree_map keyed
// by name.
class BenchmarkClusterManager : public NiceMock<MockClusterManager> {
public:
  void addCluster(std::shared_ptr<BenchmarkCluster> cluster) {
    clusters_by_name_.emplace(cluster->info()->name(), std::move(cluster));
  }

  ClusterInfoMaps clusters() const override {
    ClusterInfoMaps maps;
    for (const auto& [name, cluster] : clusters_by_name_) {
      maps.active_clusters_.emplace(name, *cluster);
    }
    return maps;
  }

  void forEachActiveCluster(std::function<void(const Cluster&)> cb) const override {
    for (const auto& [unused_name, cluster] : clusters_by_name_) {
      cb(*cluster);
    }
  }

  OptRef<const Cluster> getActiveCluster(absl::string_view cluster_name) const override {
    if (const auto it = clusters_by_name_.find(cluster_name); it != clusters_by_name_.end()) {
      return *it->second;
    }
    return std::nullopt;
  }

private:
  // Not named active_clusters_ so as not to shadow MockClusterManager::active_clusters_.
  absl::btree_map<std::string, std::shared_ptr<BenchmarkCluster>> clusters_by_name_;
};

// Hosts the mocks a LoadStatsReporterImpl needs and captures its response timer callback, which
// sends a load report and starts the next load report period.
class BenchmarkFixture {
public:
  BenchmarkFixture() {
    ON_CALL(dispatcher_, createTimer_(_)).WillByDefault([this](Event::TimerCb cb) {
      // The reporter creates the retry timer first and the response timer second.
      if (retry_timer_cb_ == nullptr) {
        retry_timer_cb_ = cb;
      } else {
        response_timer_cb_ = cb;
      }
      return new NiceMock<Event::MockTimer>();
    });
    ON_CALL(*async_client_, startRaw(_, _, _, _)).WillByDefault(Return(&async_stream_));
  }

  std::shared_ptr<BenchmarkCluster> addCluster(const std::string& name, int hosts_per_cluster) {
    auto cluster = std::make_shared<BenchmarkCluster>(name);
    if (hosts_per_cluster > 0) {
      MockHostSet& host_set = *cluster->prioritySet().getMockHostSet(0);
      HostVector hosts;
      hosts.reserve(hosts_per_cluster);
      for (int i = 0; i < hosts_per_cluster; i++) {
        hosts.push_back(
            makeTestHost(cluster->info_, absl::StrCat("tcp://10.0.0.1:", 1000 + i), locality_));
      }
      host_set.hosts_ = hosts;
      host_set.hosts_per_locality_ = makeHostsPerLocality({std::move(hosts)});
    }
    cm_.addCluster(cluster);
    return cluster;
  }

  void createReporter() {
    reporter_ = std::make_unique<LoadStatsReporterImpl>(local_info_, cm_, *stats_store_.rootScope(),
                                                        async_client_, dispatcher_);
  }

  void
  deliverResponse(std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse> response) {
    reporter_->onReceiveMessage(std::move(response));
  }

  void deliverResponse(const envoy::service::load_stats::v3::LoadStatsResponse& response) {
    deliverResponse(std::make_unique<envoy::service::load_stats::v3::LoadStatsResponse>(response));
  }

  void sendLoadReport() { response_timer_cb_(); }

private:
  Event::SimulatedTimeSystem time_system_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  BenchmarkClusterManager cm_;
  Stats::IsolatedStoreImpl stats_store_;
  std::shared_ptr<NiceMock<Grpc::MockAsyncClient>> async_client_{
      std::make_shared<NiceMock<Grpc::MockAsyncClient>>()};
  NiceMock<Grpc::MockAsyncStream> async_stream_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  envoy::config::core::v3::Locality locality_;
  Event::TimerCb retry_timer_cb_;
  Event::TimerCb response_timer_cb_;
  std::unique_ptr<LoadStatsReporterImpl> reporter_;
};

std::vector<std::string> clusterNames(int num_clusters) {
  std::vector<std::string> names;
  names.reserve(num_clusters);
  for (int i = 0; i < num_clusters; i++) {
    names.push_back(absl::StrCat("cluster_", i));
  }
  return names;
}

envoy::service::load_stats::v3::LoadStatsResponse
makeResponse(bool send_all_clusters, const std::vector<std::string>& cluster_names) {
  envoy::service::load_stats::v3::LoadStatsResponse response;
  response.set_send_all_clusters(send_all_clusters);
  response.mutable_load_reporting_interval()->set_seconds(10);
  if (!send_all_clusters) {
    for (const std::string& name : cluster_names) {
      response.add_clusters(name);
    }
  }
  return response;
}

// Measures the cost of starting a load report period, i.e. of (re)tracking the clusters selected
// by the LRS server, which happens after every load report. The fixture setup (thousands of mock
// clusters) dominates the wall time of this benchmark, not the measured code.
void bmStartLoadReportPeriod(::benchmark::State& state, bool send_all_clusters) {
  const int num_clusters = state.range(0);
  if (benchmark::skipExpensiveBenchmarks() && num_clusters > 64) {
    state.SkipWithError("Skipping expensive benchmark in test mode");
    return;
  }
  BenchmarkFixture fixture;
  const std::vector<std::string> names = clusterNames(num_clusters);
  for (const std::string& name : names) {
    fixture.addCluster(name, /*hosts_per_cluster=*/0);
  }
  fixture.createReporter();
  const envoy::service::load_stats::v3::LoadStatsResponse response =
      makeResponse(send_all_clusters, names);
  fixture.deliverResponse(response);

  for (auto _ : state) { // NOLINT
    // Copying the response (which lists every cluster when not using send_all_clusters) is not
    // part of what is measured.
    state.PauseTiming();
    auto message = std::make_unique<envoy::service::load_stats::v3::LoadStatsResponse>(response);
    state.ResumeTiming();
    fixture.deliverResponse(std::move(message));
  }
}

// Measures the cost of a load report for all clusters when a share of the clusters had requests
// since the last report.
void bmSendLoadStatsRequest(::benchmark::State& state, bool skip_idle_clusters,
                            double active_cluster_ratio) {
  const int num_clusters = state.range(0);
  const int hosts_per_cluster = state.range(1);
  if (benchmark::skipExpensiveBenchmarks() && (num_clusters > 64 || hosts_per_cluster > 10)) {
    state.SkipWithError("Skipping expensive benchmark in test mode");
    return;
  }
  TestScopedRuntime scoped_runtime;
  if (skip_idle_clusters) {
    scoped_runtime.mergeValues({{"envoy.reloadable_features.lrs_skip_idle_clusters", "true"}});
  }

  BenchmarkFixture fixture;
  std::vector<std::shared_ptr<BenchmarkCluster>> clusters;
  for (const std::string& name : clusterNames(num_clusters)) {
    clusters.push_back(fixture.addCluster(name, hosts_per_cluster));
  }
  fixture.createReporter();
  fixture.deliverResponse(makeResponse(/*send_all_clusters=*/true, {}));
  // The first report visits every cluster once.
  fixture.sendLoadReport();

  const int active_clusters = static_cast<int>(num_clusters * active_cluster_ratio);
  for (auto _ : state) { // NOLINT
    state.PauseTiming();
    for (int i = 0; i < active_clusters; i++) {
      const BenchmarkCluster& cluster = *clusters[i];
      for (const HostSharedPtr& host :
           cluster.prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get()[0]) {
        // What the data plane records for a request that was issued, completed successfully and
        // carried an ORCA load report. The hosts are real HostImpls, so recording the load metric
        // also counts a host load update on the cluster.
        host->stats().rq_total_.inc();
        host->stats().rq_success_.inc();
        host->loadMetricStats().add("cpu_utilization", 0.5);
      }
    }
    state.ResumeTiming();
    fixture.sendLoadReport();
  }
}

// The mock clusters are expensive to set up, so the number of clusters is kept moderate and the
// number of hosts per cluster is the main knob for the total number of hosts.
BENCHMARK_CAPTURE(bmStartLoadReportPeriod, SendAllClusters, true)->Range(8, 512);
BENCHMARK_CAPTURE(bmStartLoadReportPeriod, ExplicitClusters, false)->Range(8, 512);

// {clusters, hosts per cluster}
#define SEND_LOAD_STATS_REQUEST_RANGES Ranges({{8, 512}, {8, 512}})

BENCHMARK_CAPTURE(bmSendLoadStatsRequest, IdleClusters_VisitAll, false, 0.0)
    ->SEND_LOAD_STATS_REQUEST_RANGES;
BENCHMARK_CAPTURE(bmSendLoadStatsRequest, IdleClusters_SkipIdle, true, 0.0)
    ->SEND_LOAD_STATS_REQUEST_RANGES;
BENCHMARK_CAPTURE(bmSendLoadStatsRequest, TenPercentActive_VisitAll, false, 0.1)
    ->SEND_LOAD_STATS_REQUEST_RANGES;
BENCHMARK_CAPTURE(bmSendLoadStatsRequest, TenPercentActive_SkipIdle, true, 0.1)
    ->SEND_LOAD_STATS_REQUEST_RANGES;
BENCHMARK_CAPTURE(bmSendLoadStatsRequest, AllActive_VisitAll, false, 1.0)
    ->SEND_LOAD_STATS_REQUEST_RANGES;
BENCHMARK_CAPTURE(bmSendLoadStatsRequest, AllActive_SkipIdle, true, 1.0)
    ->SEND_LOAD_STATS_REQUEST_RANGES;

} // namespace
} // namespace Upstream
} // namespace Envoy
