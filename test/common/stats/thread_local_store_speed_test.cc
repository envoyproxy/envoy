// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/server/server.h"

#include "test/common/stats/real_thread_test_base.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {

class InitBase {
 public:
  InitBase() {
    if (!Event::Libevent::Global::initialized()) {
      Event::Libevent::Global::initialize();
    }
  }
};

class ThreadLocalStorePerf : public InitBase {
public:
  ThreadLocalStorePerf()
      : heap_alloc_(symbol_table_), store_(heap_alloc_),
        api_(Api::createApiForTest(store_, time_system_)) {
    const Stats::TagVector tags;
    store_.setTagProducer(Stats::TagProducerImpl::createTagProducer(stats_config_, tags).value());

    Stats::TestUtil::forEachSampleStat(1000, true, [this](absl::string_view name) {
      stat_names_.push_back(std::make_unique<Stats::StatNameManagedStorage>(name, symbol_table_));
    });
  }

  ~ThreadLocalStorePerf() {
    if (tls_) {
      tls_->shutdownGlobalThreading();
    }
    store_.shutdownThreading();
    if (tls_) {
      tls_->shutdownThread();
    }
    if (dispatcher_) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void accessCounters() {
    Stats::Scope& scope = *store_.rootScope();
    for (auto& stat_name_storage : stat_names_) {
      scope.counterFromStatName(stat_name_storage->statName());
    }
  }

  void initThreading() {
    dispatcher_ = api_->allocateDispatcher("test_thread");
    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    tls_->registerThread(*dispatcher_, true);
    store_.initializeThreading(*dispatcher_, *tls_);
  }

  void initPrefixRejections(const std::string& prefix) {
    stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
        prefix);
    store_.setStatsMatcher(
        std::make_unique<Stats::StatsMatcherImpl>(stats_config_, symbol_table_, context_));
  }

private:
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::SymbolTableImpl symbol_table_;
  Event::SimulatedTimeSystem time_system_;
  Stats::AllocatorImpl heap_alloc_;
  Event::DispatcherPtr dispatcher_;
  ThreadLocal::InstanceImplPtr tls_;
  Stats::ThreadLocalStoreImpl store_;
  Api::ApiPtr api_;
  envoy::config::metrics::v3::StatsConfig stats_config_;
  std::vector<std::unique_ptr<Stats::StatNameManagedStorage>> stat_names_;
};

} // namespace Envoy

// Tests the single-threaded performance of the thread-local-store stats caches
// without having initialized tls.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsNoTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsNoTls);

// Tests the single-threaded performance of the thread-local-store stats caches
// with tls. Note that this test is still single-threaded, and so there's only
// one replica of the tls cache.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTls);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTlsAndRejectionsWithDot(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();
  context.initPrefixRejections("cluster.");

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTlsAndRejectionsWithDot);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTlsAndRejectionsWithoutDot(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();
  context.initPrefixRejections("cluster");

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTlsAndRejectionsWithoutDot);

// TODO(jmarantz): add multi-threaded variant of this test, that aggressively
// looks up stats in multiple threads to try to trigger contention issues.


// Attempts to Reproduce #37831 in a benchmark test. In that issue,
// we have observed that histogram sinking to DataDog that occurs on
// every call to recordValue results in contention on stats tags. The
// DataDog sink is a simple derivation of UdpStatssSink, but even that is
// too complex for the benchmark, so we will create our own test-only
// stats sink.

class TestSinkForHistogramContention : public Envoy::Stats::Sink {
 public:
  static bool buffer_histograms_;
  static constexpr size_t max_buffered_entries_ = 50000;

  void reset() {
    total_bytes_ = 0;
  }

  void printTotal() {
    ENVOY_LOG_MISC(error, "total_bytes={}", total_bytes_);
  }

  void flush(Envoy::Stats::MetricSnapshot&) override {
    flushHistograms();
  }

  void flushHistograms() {
    if (buffer_histograms_) {
      absl::MutexLock lock(&mutex_);
      flushHistogramsLockHeld();
    }
  }

  void onHistogramComplete(const Envoy::Stats::Histogram& histogram, uint64_t value) override {
    if (buffer_histograms_) {
      absl::MutexLock lock(&mutex_);
      buffer_[&histogram].push_back(value);
      if (++buffered_entries_ == max_buffered_entries_) {
        flushHistogramsLockHeld();
      }
    } else {
      // All we need to repro the problem is to request the stat name and
      // tags.
      absl::MutexLock lock(&mutex_);
      accumulateBytesLockHeld(histogram, 1);
    }
  }

  void flushHistogramsLockHeld() {
    //ENVOY_LOG_MISC(error, "Flushing histograms...");

    for (auto& iter : buffer_) {
      accumulateBytesLockHeld(*iter.first, iter.second.size());
    }
    buffer_.clear();
    buffered_entries_ = 0;
  }

  void accumulateBytesLockHeld(const Envoy::Stats::Histogram& histogram, size_t multiplier) {
    total_bytes_ += multiplier * histogram.name().size();
    for (const Envoy::Stats::Tag& tag : histogram.tags()) {
     total_bytes_ += multiplier * (tag.name_.size() + tag.value_.size());
    }
  }

  absl::flat_hash_map<const Envoy::Stats::Histogram*, std::vector<uint64_t>> buffer_;
  absl::Mutex mutex_;
  int buffered_entries_{0};
  int total_bytes_{0};
};

bool TestSinkForHistogramContention::buffer_histograms_ = false;

// BM_ExportOnRecordValue   27664026 ns        60752 ns          100
// BM_ExportOnFlush          3757351 ns        50485 ns         1000
//
// This benchmark exaggerates the problem by having all 10 worker
// threads constantly calling recordValue, which will cause contention.
// Note that the elapsed time for BM_ExportOnRecordValue is 7x slower
// than for BM_ExportOnFlush. This is likely because the delays are
// due to mutex contention, rather than CPU.

/*
#include <atomic>
#include <chrono>  // NOLINT
#include <cstdint>
#include <memory>

#include "base/logging.h"
#include "net/envoy/source/extensions/stat_sinks/monarch/export_server.h"
#include "net/envoy/source/extensions/stat_sinks/monarch/monarch_sink.h"
#include "net/envoy/source/extensions/stat_sinks/monarch/options.proto.h"
#include "testing/base/public/gmock.h"
#include "third_party/absl/flags/flag.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"
#include "third_party/envoy/src/envoy/event/dispatcher.h"
#include "third_party/envoy/src/envoy/stats/scope.h"
#include "third_party/envoy/src/envoy/stats/stats_macros.h"
#include "third_party/envoy/src/envoy/stats/store.h"
#include "third_party/envoy/src/source/exe/process_wide.h"
#include "third_party/envoy/src/source/server/server.h"
#include "third_party/envoy/src/test/mocks/upstream/cluster_manager.h"
*/

// Captures context needed to run a histogram sync benchmark.
class StatsDHistogramBenchmarkTest :
    public Envoy::InitBase, public Envoy::Stats::ThreadLocalRealThreadsMixin {
 public:
  static constexpr uint32_t kNumThreads = 30;

  // Wrapper for DispatcherStats which to make it easier to create lazily.
  struct DispatcherStats {
    explicit DispatcherStats(Envoy::Stats::Store& store) :
        dispatcher_stats{ALL_DISPATCHER_STATS(
            POOL_HISTOGRAM_PREFIX(store, "cluster.name-of-this-cluster-is-long.and.is.followed.by.more.junk"))} {}
    Envoy::Event::DispatcherStats dispatcher_stats;
  };

  StatsDHistogramBenchmarkTest()
      : Envoy::Stats::ThreadLocalRealThreadsMixin(kNumThreads) {
    store_->addSink(sink_);

    // All the Envoy API calls after construction must be from an Envoy-managed
    // thread -- the Envoy main thread is different from the test thread.
    //
    // We want to initialize the stats from Envoy's main thread, and then set
    // up a mechanism to run stats flushes periodically.
    runOnMainBlocking([this]() {
      main_thread_stats_ = std::make_unique<DispatcherStats>(*store_);
      flush_timer_ = main_dispatcher_->createTimer([this]() {
        Flush();
        QueueNextFlush();
      });
      QueueNextFlush();
    });
  }

  ~StatsDHistogramBenchmarkTest() {
    // We also do a final flush on exit.
    runOnMainBlocking([this]() { Flush(); });
    shutdownThreading();
  }

  // Flushes stats by iterating over all histograms and coalescing
  // contributions from different threads. This is always called
  // from the main thread.
  void Flush() {
    if (merging_.exchange(true)) {
      //LOG(INFO) << "Skipping stat flush as it is already queued";
      return;
    }

    ENVOY_LOG_MISC(error, "Flushing stats...");
    store_->mergeHistograms([this] {
      Envoy::Server::MetricSnapshotImpl snapshot(
          *store_, cluster_manager_, api_->timeSource());
      sink_.flush(snapshot);
      merging_ = false;
    });
  }

  // Simulates a data plane updating histograms rapidly on every
  // worker thread.
  void recordValuesOnAllWorkers() {
    sink_.reset();
    runOnAllWorkersBlocking([this]() {
      for (int i = 0; i < 10000; ++i) {
        main_thread_stats_->dispatcher_stats.loop_duration_us_.recordValue(i);
        main_thread_stats_->dispatcher_stats.poll_delay_us_.recordValue(100*i);
      }
    });
    sink_.flushHistograms();
    sink_.printTotal();
  }

  // Queues up a Flush to occur after an interval. In production we run stats
  // flush every 5 seconds, but in the benchmark we flush every second just
  // to stress the system more and invite more contention.
  void QueueNextFlush() {
    flush_timer_->enableTimer(std::chrono::milliseconds(1000));
  }

  //  Envoy::ProcessWide process_wide_;
  std::atomic<bool> merging_{false};
  TestSinkForHistogramContention sink_;
  std::unique_ptr<DispatcherStats> main_thread_stats_;
  testing::NiceMock<Envoy::Upstream::MockClusterManager> cluster_manager_;
  Envoy::Event::TimerPtr flush_timer_;
};

void BM_RecordHistogramsUnbuffered(benchmark::State& state) {
  TestSinkForHistogramContention::buffer_histograms_ = false;
  StatsDHistogramBenchmarkTest test;
  for (auto _ : state) {
    test.recordValuesOnAllWorkers();
  }
}
BENCHMARK(BM_RecordHistogramsUnbuffered);

void BM_RecordHistogramsBuffered(benchmark::State& state) {
  TestSinkForHistogramContention::buffer_histograms_ = true;
  StatsDHistogramBenchmarkTest test;
  for (auto _ : state) {
    test.recordValuesOnAllWorkers();
  }
}
BENCHMARK(BM_RecordHistogramsBuffered);
