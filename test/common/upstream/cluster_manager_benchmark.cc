#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/api/api_impl.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/thread.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/null_grpc_mux_impl.h"
#include "source/common/config/utility.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/libevent.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/common/upstream/cds_api_helper.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/cluster_manager_impl.h"

#include "test/benchmark/main.h"
#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/common/upstream/test_cluster_manager.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/real_threads_test_helper.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

static std::unique_ptr<Thread::RealThreadsTestHelper>
createRealThreadsHelper(uint32_t num_threads) {
  if (!Event::Libevent::Global::initialized()) {
    Event::Libevent::Global::initialize();
  }
  return std::make_unique<Thread::RealThreadsTestHelper>(num_threads);
}

/**
 * Multi-threaded benchmark fixture managing real worker threads and ThreadLocal::InstanceImpl
 * via Envoy's RealThreadsTestHelper.
 *
 * Simulates a realistic multi-worker Envoy server configuration where ClusterManagerImpl
 * resides on the main dispatcher thread and propagates cluster updates across worker threads.
 *
 * Benchmarking Best Practices & Noise Reduction:
 * - Build with optimizations: `-c opt --dynamic_mode=off`.
 * - Disable Address Space Layout Randomization (ASLR) to eliminate code layout and alignment noise:
 *     `setarch $(uname -m) -R <benchmark_binary>`
 * - Use multiple repetitions and adequate runtime to ensure statistical stability:
 *     `--benchmark_repetitions=10 --benchmark_min_time=2s`
 * - To isolate cluster update and cross-thread dispatch latency from benchmark test data creation,
 *   protobuf cluster and decoded resource generation is performed within `state.PauseTiming()`
 * blocks.
 */
class ClusterManagerBenchmarkFixture {
public:
  explicit ClusterManagerBenchmarkFixture(uint32_t num_threads, bool batching_enabled)
      : num_threads_(num_threads), real_threads_(createRealThreadsHelper(num_threads)) {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.batch_cluster_updates", batching_enabled ? "true" : "false"}});

    real_threads_->runOnMainBlocking([this]() {
      ON_CALL(server_context_, threadLocal()).WillByDefault(ReturnRef(real_threads_->tls()));
      ON_CALL(server_context_, mainThreadDispatcher())
          .WillByDefault(ReturnRef(real_threads_->mainDispatcher()));
      ON_CALL(server_context_, api()).WillByDefault(ReturnRef(real_threads_->api()));
      ON_CALL(server_context_, runtime()).WillByDefault(ReturnRef(scoped_runtime_.loader()));

      factory_ = std::make_unique<NiceMock<TestClusterManagerFactory>>();
      ON_CALL(factory_->server_context_, threadLocal())
          .WillByDefault(ReturnRef(real_threads_->tls()));
      ON_CALL(factory_->server_context_, mainThreadDispatcher())
          .WillByDefault(ReturnRef(real_threads_->mainDispatcher()));
      ON_CALL(factory_->server_context_, api()).WillByDefault(ReturnRef(real_threads_->api()));
      ON_CALL(factory_->server_context_, runtime())
          .WillByDefault(ReturnRef(scoped_runtime_.loader()));

      const auto bootstrap = defaultConfig();
      cm_ = TestClusterManagerImpl::createTestClusterManager(bootstrap, *factory_, server_context_);
      ON_CALL(server_context_, clusterManager()).WillByDefault(ReturnRef(*cm_));
      xds_manager_ = std::make_unique<NiceMock<Config::MockXdsManager>>();
      cds_helper_ = std::make_unique<CdsApiHelper>(*cm_, *xds_manager_, "benchmark_cds_helper");
    });
  }

  ~ClusterManagerBenchmarkFixture() {
    real_threads_->runOnMainBlocking([this]() {
      if (cm_ != nullptr) {
        cm_->shutdown();
      }
      cds_helper_.reset();
      xds_manager_.reset();
      cm_.reset();
      factory_.reset();
    });
    real_threads_->shutdownThreading();
    real_threads_->exitThreads();
  }

  void runOnMainBlocking(std::function<void()> work) { real_threads_->runOnMainBlocking(work); }

  void drainAll() { real_threads_->tlsBlock(); }

  std::vector<envoy::config::cluster::v3::Cluster>
  createClusters(uint32_t count, const std::string& prefix = "bench_cluster_") {
    std::vector<envoy::config::cluster::v3::Cluster> clusters;
    clusters.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      clusters.push_back(defaultStaticCluster(absl::StrCat(prefix, i)));
    }
    return clusters;
  }

  std::vector<envoy::config::cluster::v3::Cluster>
  createUpdatedClusters(const std::vector<envoy::config::cluster::v3::Cluster>& base_clusters) {
    std::vector<envoy::config::cluster::v3::Cluster> updated_clusters;
    updated_clusters.reserve(base_clusters.size());
    for (const auto& base : base_clusters) {
      envoy::config::cluster::v3::Cluster updated = base;
      // Modify connect timeout to produce distinct config hash and trigger update path.
      updated.mutable_connect_timeout()->set_nanos(500000000);
      updated_clusters.push_back(std::move(updated));
    }
    return updated_clusters;
  }

  struct DecodedResourcesWrapper {
    std::vector<Config::DecodedResourceImplPtr> owned_resources;
    std::vector<Config::DecodedResourceRef> resource_refs;
  };

  DecodedResourcesWrapper
  createDecodedResources(const std::vector<envoy::config::cluster::v3::Cluster>& clusters,
                         const std::string& version) {
    DecodedResourcesWrapper wrapper;
    wrapper.owned_resources.reserve(clusters.size());
    wrapper.resource_refs.reserve(clusters.size());
    for (const auto& cluster : clusters) {
      auto owned = std::make_unique<Config::DecodedResourceImpl>(
          std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), cluster.name(),
          std::vector<std::string>{}, version);
      wrapper.resource_refs.push_back(*owned);
      wrapper.owned_resources.push_back(std::move(owned));
    }
    return wrapper;
  }

  void applyConfigUpdate(const DecodedResourcesWrapper& added,
                         const Protobuf::RepeatedPtrField<std::string>& removed,
                         const std::string& version) {
    runOnMainBlocking([this, &added, &removed, &version]() {
      cds_helper_->onConfigUpdate(added.resource_refs, removed, version);
    });
    drainAll();
  }

  Thread::RealThreadsTestHelper& threads() { return *real_threads_; }
  uint32_t numThreads() const { return num_threads_; }

private:
  const uint32_t num_threads_;
  TestScopedRuntime scoped_runtime_;
  std::unique_ptr<Thread::RealThreadsTestHelper> real_threads_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::unique_ptr<NiceMock<TestClusterManagerFactory>> factory_;
  std::unique_ptr<TestClusterManagerImpl> cm_;
  std::unique_ptr<NiceMock<Config::MockXdsManager>> xds_manager_;
  std::unique_ptr<CdsApiHelper> cds_helper_;
};

/**
 * Fixture for measuring direct thread local slot cross-thread dispatch contention.
 */
class DirectTlsBenchmarkFixture {
public:
  explicit DirectTlsBenchmarkFixture(uint32_t num_threads)
      : num_threads_(num_threads), real_threads_(createRealThreadsHelper(num_threads)) {
    real_threads_->runOnMainBlocking([this]() {
      slot_ = ThreadLocal::TypedSlot<ThreadState>::makeUnique(real_threads_->tls());
      slot_->set([](Event::Dispatcher&) -> std::shared_ptr<ThreadState> {
        return std::make_shared<ThreadState>();
      });
    });
  }

  struct ThreadState : public ThreadLocal::ThreadLocalObject {
    uint64_t counter_{0};
  };

  ~DirectTlsBenchmarkFixture() {
    real_threads_->runOnMainBlocking([this]() { slot_.reset(); });
    real_threads_->shutdownThreading();
    real_threads_->exitThreads();
  }

  void dispatchUnbatched(uint32_t num_actions) {
    real_threads_->runOnMainBlocking([this, num_actions]() {
      for (uint32_t i = 0; i < num_actions; ++i) {
        slot_->runOnAllThreads([](OptRef<ThreadState> obj) {
          if (obj.has_value()) {
            obj->counter_++;
          }
        });
      }
    });
    real_threads_->tlsBlock();
  }

  void dispatchBatched(uint32_t num_actions) {
    real_threads_->runOnMainBlocking([this, num_actions]() {
      slot_->runOnAllThreads([num_actions](OptRef<ThreadState> obj) {
        if (obj.has_value()) {
          for (uint32_t i = 0; i < num_actions; ++i) {
            obj->counter_++;
          }
        }
      });
    });
    real_threads_->tlsBlock();
  }

  uint32_t numThreads() const { return num_threads_; }

private:
  const uint32_t num_threads_;
  std::unique_ptr<Thread::RealThreadsTestHelper> real_threads_;
  ThreadLocal::TypedSlotPtr<ThreadState> slot_;
};

// --- Benchmark Functions ---

/**
 * Measures the latency of adding K new dynamic clusters via CDS onConfigUpdate across M worker
 * threads.
 *
 * In unbatched mode, each cluster addition triggers an independent thread-local cluster allocation
 * and priority set update across all M worker threads. In batched mode, all K additions are
 * deferred and dispatched in a single batch TLS callback.
 */
static void BM_CdsClusterAddition(::benchmark::State& state, bool batching_enabled) {
  const uint32_t cluster_count = state.range(0);
  const uint32_t thread_count = state.range(1);

  for (auto _ : state) {
    state.PauseTiming();
    ClusterManagerBenchmarkFixture fixture(thread_count, batching_enabled);
    auto clusters = fixture.createClusters(cluster_count);
    auto resources = fixture.createDecodedResources(clusters, "v1");
    Protobuf::RepeatedPtrField<std::string> empty_removed;
    state.ResumeTiming();

    fixture.applyConfigUpdate(resources, empty_removed, "v1");
  }

  state.SetItemsProcessed(state.iterations() * cluster_count);
  state.counters["clusters"] = cluster_count;
  state.counters["threads"] = thread_count;
}

static void BM_CdsClusterAdditionBatched(::benchmark::State& state) {
  BM_CdsClusterAddition(state, true);
}
static void BM_CdsClusterAdditionUnbatched(::benchmark::State& state) {
  BM_CdsClusterAddition(state, false);
}

/**
 * Measures the latency of modifying K existing dynamic clusters via CDS onConfigUpdate across M
 * worker threads.
 *
 * Evaluates warming cluster state transition and member/priority updates. Batched mode coalesces
 * thread-local cluster modifications and load balancer reinitializations into a single TLS
 * dispatch.
 */
static void BM_CdsClusterUpdate(::benchmark::State& state, bool batching_enabled) {
  const uint32_t cluster_count = state.range(0);
  const uint32_t thread_count = state.range(1);

  for (auto _ : state) {
    state.PauseTiming();
    ClusterManagerBenchmarkFixture fixture(thread_count, batching_enabled);
    auto initial_clusters = fixture.createClusters(cluster_count);
    auto initial_resources = fixture.createDecodedResources(initial_clusters, "v1");
    Protobuf::RepeatedPtrField<std::string> empty_removed;
    fixture.applyConfigUpdate(initial_resources, empty_removed, "v1");

    auto updated_clusters = fixture.createUpdatedClusters(initial_clusters);
    auto updated_resources = fixture.createDecodedResources(updated_clusters, "v2");
    state.ResumeTiming();

    fixture.applyConfigUpdate(updated_resources, empty_removed, "v2");
  }

  state.SetItemsProcessed(state.iterations() * cluster_count);
  state.counters["clusters"] = cluster_count;
  state.counters["threads"] = thread_count;
}

static void BM_CdsClusterUpdateBatched(::benchmark::State& state) {
  BM_CdsClusterUpdate(state, true);
}
static void BM_CdsClusterUpdateUnbatched(::benchmark::State& state) {
  BM_CdsClusterUpdate(state, false);
}

/**
 * Measures the latency of removing K active clusters via CDS onConfigUpdate across M worker
 * threads.
 *
 * In unbatched mode, each cluster removal posts an independent TLS cleanup callback to all M
 * workers. In batched mode, removals are accumulated in pending_thread_local_actions_ and drained
 * in a single TLS callback upon batch completion.
 */
static void BM_CdsClusterRemoval(::benchmark::State& state, bool batching_enabled) {
  const uint32_t cluster_count = state.range(0);
  const uint32_t thread_count = state.range(1);

  for (auto _ : state) {
    state.PauseTiming();
    ClusterManagerBenchmarkFixture fixture(thread_count, batching_enabled);
    auto clusters = fixture.createClusters(cluster_count);
    auto resources = fixture.createDecodedResources(clusters, "v1");
    Protobuf::RepeatedPtrField<std::string> empty_removed;
    fixture.applyConfigUpdate(resources, empty_removed, "v1");

    Protobuf::RepeatedPtrField<std::string> removed_clusters;
    for (const auto& cluster : clusters) {
      removed_clusters.Add(std::string(cluster.name()));
    }
    ClusterManagerBenchmarkFixture::DecodedResourcesWrapper empty_added;
    state.ResumeTiming();

    fixture.applyConfigUpdate(empty_added, removed_clusters, "v2");
  }

  state.SetItemsProcessed(state.iterations() * cluster_count);
  state.counters["clusters"] = cluster_count;
  state.counters["threads"] = thread_count;
}

static void BM_CdsClusterRemovalBatched(::benchmark::State& state) {
  BM_CdsClusterRemoval(state, true);
}
static void BM_CdsClusterRemovalUnbatched(::benchmark::State& state) {
  BM_CdsClusterRemoval(state, false);
}

/**
 * Direct benchmark of raw ThreadLocal::Slot cross-thread dispatch latency.
 *
 * Measures the pure dispatch and event loop queueing overhead of K cross-thread actions across
 * M worker threads without cluster manager or configuration parsing overhead.
 */
static void BM_DirectTlsDispatch(::benchmark::State& state, bool batched) {
  const uint32_t action_count = state.range(0);
  const uint32_t thread_count = state.range(1);

  DirectTlsBenchmarkFixture fixture(thread_count);

  for (auto _ : state) {
    if (batched) {
      fixture.dispatchBatched(action_count);
    } else {
      fixture.dispatchUnbatched(action_count);
    }
  }

  state.SetItemsProcessed(state.iterations() * action_count);
  state.counters["actions"] = action_count;
  state.counters["threads"] = thread_count;
}

static void BM_DirectTlsDispatchBatched(::benchmark::State& state) {
  BM_DirectTlsDispatch(state, true);
}
static void BM_DirectTlsDispatchUnbatched(::benchmark::State& state) {
  BM_DirectTlsDispatch(state, false);
}

static void ClusterArgs(::benchmark::internal::Benchmark* b) {
  for (int threads : {1, 4, 8, 16}) {
    for (int clusters : {10, 50, 100, 500}) {
      b->Args({clusters, threads});
    }
  }
}

static void TlsArgs(::benchmark::internal::Benchmark* b) {
  for (int threads : {1, 4, 8, 16}) {
    for (int actions : {10, 50, 100, 500, 1000}) {
      b->Args({actions, threads});
    }
  }
}

BENCHMARK(BM_CdsClusterAdditionBatched)->Apply(ClusterArgs)->Unit(::benchmark::kMillisecond);
BENCHMARK(BM_CdsClusterAdditionUnbatched)->Apply(ClusterArgs)->Unit(::benchmark::kMillisecond);
BENCHMARK(BM_CdsClusterUpdateBatched)->Apply(ClusterArgs)->Unit(::benchmark::kMillisecond);
BENCHMARK(BM_CdsClusterUpdateUnbatched)->Apply(ClusterArgs)->Unit(::benchmark::kMillisecond);
BENCHMARK(BM_CdsClusterRemovalBatched)->Apply(ClusterArgs)->Unit(::benchmark::kMillisecond);
BENCHMARK(BM_CdsClusterRemovalUnbatched)->Apply(ClusterArgs)->Unit(::benchmark::kMillisecond);

BENCHMARK(BM_DirectTlsDispatchBatched)->Apply(TlsArgs)->Unit(::benchmark::kMicrosecond);
BENCHMARK(BM_DirectTlsDispatchUnbatched)->Apply(TlsArgs)->Unit(::benchmark::kMicrosecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
