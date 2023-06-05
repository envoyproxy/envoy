#include <cstdlib>
#include <sys/types.h>

#include <memory>
#include <string>

#include "source/common/network/address_impl.h"
#include "source/extensions/stat_sinks/common/statsd/statsd.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {
namespace {

enum SinkOptions {
  TcpCounterGaugesSink,
  UdpCounterGaugesSink,
  TcpHistogramComplete,
  UdpHistogramComplete,
};
// set this value to percentage of stats requiring sanitization 0-100%
int percent_requiring_sanitization = 10;

class StatsdSinkTest {
public:
  StatsdSinkTest(SinkOptions sink_option, int num_stats)
      : sink_option_(sink_option), num_stats_(num_stats) {
    if (sink_option_ == SinkOptions::TcpCounterGaugesSink ||
        sink_option_ == SinkOptions::TcpHistogramComplete) {
      cluster_manager_.initializeClusters({"fake_cluster"}, {});
      cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
      sink_ = std::make_unique<TcpStatsdSink>(
          local_info_, "fake_cluster", tls_, cluster_manager_,
          *(cluster_manager_.active_clusters_["fake_cluster"]->info_->stats_store_.rootScope()));
    } else {
      // UDP Sink
      auto uds_address = std::make_shared<Network::Address::PipeInstance>(
          TestEnvironment::unixDomainSocketPath("udstest.1.sock"));
      sink_ = std::make_unique<UdpStatsdSink>(tls_, uds_address, false);
    }

    if (sink_option_ == SinkOptions::TcpCounterGaugesSink ||
        sink_option_ == SinkOptions::UdpCounterGaugesSink) {
      this->createSnapshotWithCounterAndGauges();
    }
  }

  ~StatsdSinkTest() = default;

  void histogramPushTest(::benchmark::State& state) {
    int idx = 1;
    for (auto _ : state) {  //  NOLINT
      NiceMock<Stats::MockHistogram> duration_micro;
      duration_micro.name_ = "micro#duration_" + std::to_string(idx);
      duration_micro.unit_ = Stats::Histogram::Unit::Microseconds;
      sink_->onHistogramComplete(duration_micro, idx);
      idx++;
    }
  }

  void flushTest(::benchmark::State& state) {
    for (auto _ : state) {  //  NOLINT
      sink_->flush(snapshot_);
    }
  }

private:
  void createSnapshotWithCounterAndGauges() {
    int sanitization_count = int(percent_requiring_sanitization * num_stats_) / 100;
    counter1_.name_ = "test_counter_name_";
    counter1_.used_ = true;
    gauge1_.name_ = "test_gauge_name_";
    gauge1_.used_ = true;
    counter2_.name_ = "test:counter@name_";
    counter2_.used_ = true;
    gauge2_.name_ = "test:gauge@name_";
    gauge2_.used_ = true;
    //  add repeated counter and gauge locations that do not require metric name sanitization
    for (int idx = 0; idx < num_stats_ - sanitization_count; ++idx) {
      counter1_.value_ = idx;
      counter1_.latch_ = idx;
      gauge1_.value_ = idx;
      snapshot_.counters_.push_back({static_cast<uint64_t>(idx), counter1_});
      snapshot_.gauges_.push_back(gauge1_);
    }
    //  add repeated counter and gauge locations that require metric name sanitization
    for (int idx = 0; idx < sanitization_count; ++idx) {
      counter2_.value_ = idx;
      counter2_.latch_ = idx;
      snapshot_.counters_.push_back({static_cast<uint64_t>(idx), counter2_});
      gauge2_.value_ = idx;
      snapshot_.gauges_.push_back(gauge2_);
    }
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<Stats::Sink> sink_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Stats::MockGauge> gauge1_;
  NiceMock<Stats::MockCounter> counter1_;
  NiceMock<Stats::MockGauge> gauge2_;
  NiceMock<Stats::MockCounter> counter2_;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  SinkOptions sink_option_;
  int num_stats_;
};

static void bmTCPStatsdSinktest(::benchmark::State& state) {
  StatsdSinkTest statsd_speed_test(SinkOptions::TcpCounterGaugesSink, state.range(0));
  statsd_speed_test.flushTest(state);
}

static void bmUDPStatsdSinktest(::benchmark::State& state) {
  StatsdSinkTest statsd_speed_test(SinkOptions::UdpCounterGaugesSink, state.range(0));
  statsd_speed_test.flushTest(state);
}

static void bmTCPStatsdOnHistogramComplete(::benchmark::State& state) {
  StatsdSinkTest statsd_speed_test(SinkOptions::TcpHistogramComplete, 0);
  statsd_speed_test.histogramPushTest(state);
}

static void bmUDPStatsdOnHistogramComplete(::benchmark::State& state) {
  StatsdSinkTest statsd_speed_test(SinkOptions::UdpHistogramComplete, 0);
  statsd_speed_test.histogramPushTest(state);
}

BENCHMARK(bmTCPStatsdSinktest)
    ->Unit(::benchmark::kMillisecond)
    ->RangeMultiplier(10)
    ->Range(100, 1000000);
BENCHMARK(bmUDPStatsdSinktest)
    ->Unit(::benchmark::kMillisecond)
    ->RangeMultiplier(10)
    ->Range(100, 1000000);
BENCHMARK(bmTCPStatsdOnHistogramComplete)->Unit(::benchmark::kMicrosecond)->Iterations(100000);
BENCHMARK(bmUDPStatsdOnHistogramComplete)->Unit(::benchmark::kMicrosecond)->Iterations(100000);
} // namespace
} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
