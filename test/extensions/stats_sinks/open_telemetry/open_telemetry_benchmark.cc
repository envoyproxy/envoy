// This file benchmarks the OpenTelemetry stats sink using the `Stats::Sink::flush` API.
// The benchmark runs at a scale of 9,000 stats (3,000 counters, 3,000 gauges, 3,000 histograms)
// with 8 tags each to test end-to-end aggregation and RPC generation performance
// under realistic load with high cardinality.

#include "envoy/stats/tag.h"

#include "source/common/stats/histogram_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/stats/mocks.h"

#include "absl/container/inlined_vector.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {
namespace {

const std::vector<Stats::Tag> test_tags = {{"cluster_name", "bar_cluster"},
                                           {"response_code", "200"},
                                           {"uri", "/api/v1/resource"},
                                           {"method", "GET"},
                                           {"zone", "us-east1-a"},
                                           {"tag6", "value6"},
                                           {"tag7", "value7"},
                                           {"tag8", "value8"}};

class DummyExporter : public OtlpMetricsExporter {
public:
  void send(MetricsExportRequestPtr&&) override {}
};

struct BenchmarkSetup {
  BenchmarkSetup() {
    snapshot = std::make_unique<testing::NiceMock<Stats::MockMetricSnapshot>>();
    hist = hist_alloc();
    hist_insert(hist, 1.5, 10);
    hist_insert(hist, 2.5, 20);
    hist_stats = std::make_unique<Stats::HistogramStatisticsImpl>(hist);

    const int num_counters = 3000;
    const int num_gauges = 3000;
    const int num_histograms = 3000;

    counters.reserve(num_counters);
    gauges.reserve(num_gauges);
    histograms.reserve(num_histograms);

    auto make_tags = [](int i) {
      std::vector<Stats::Tag> tags = test_tags;
      tags.back().value_ = std::to_string(i);
      return tags;
    };

    for (int i = 0; i < num_counters; ++i) {
      auto counter = std::make_unique<testing::NiceMock<Stats::MockCounter>>();
      counter->name_ = "counter_metric";
      counter->setTagExtractedName("counter_metric");
      counter->value_ = 100;
      counter->used_ = true;
      counter->setTags(make_tags(i));
      snapshot->counters_.push_back({1, *counter});
      counters.push_back(std::move(counter));
    }

    for (int i = 0; i < num_gauges; ++i) {
      auto gauge = std::make_unique<testing::NiceMock<Stats::MockGauge>>();
      gauge->name_ = "gauge_metric";
      gauge->setTagExtractedName("gauge_metric");
      gauge->value_ = 100;
      gauge->used_ = true;
      gauge->setTags(make_tags(i));
      snapshot->gauges_.push_back(*gauge);
      gauges.push_back(std::move(gauge));
    }

    for (int i = 0; i < num_histograms; ++i) {
      auto histogram = std::make_unique<testing::NiceMock<Stats::MockParentHistogram>>();
      histogram->name_ = "hist_metric";
      histogram->setTagExtractedName("hist_metric");
      histogram->used_ = true;
      histogram->setTags(make_tags(i));
      ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(testing::ReturnRef(*hist_stats));
      snapshot->histograms_.push_back(*histogram);
      histograms.push_back(std::move(histogram));
    }
  }

  ~BenchmarkSetup() { hist_free(hist); }

  std::unique_ptr<testing::NiceMock<Stats::MockMetricSnapshot>> snapshot;
  histogram_t* hist;
  std::unique_ptr<Stats::HistogramStatisticsImpl> hist_stats;
  std::vector<std::unique_ptr<testing::NiceMock<Stats::MockCounter>>> counters;
  std::vector<std::unique_ptr<testing::NiceMock<Stats::MockGauge>>> gauges;
  std::vector<std::unique_ptr<testing::NiceMock<Stats::MockParentHistogram>>> histograms;
};

void BM_OpenTelemetrySinkFlush_NoLimit(benchmark::State& state) {
  BenchmarkSetup setup;

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  Tracers::OpenTelemetry::Resource resource;

  auto options = std::make_shared<OtlpOptions>(sink_config, resource, server_factory_context);
  auto flusher = std::make_shared<OtlpMetricsFlusherImpl>(options);
  auto exporter = std::make_shared<DummyExporter>();

  OpenTelemetrySink sink(flusher, exporter, 0);

  for (auto _ : state) {
    sink.flush(*setup.snapshot);
  }
}
BENCHMARK(BM_OpenTelemetrySinkFlush_NoLimit);

void BM_OpenTelemetrySinkFlush_TrafficSplit_200(benchmark::State& state) {
  BenchmarkSetup setup;

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.set_max_data_points_per_request(200);
  Tracers::OpenTelemetry::Resource resource;

  auto options = std::make_shared<OtlpOptions>(sink_config, resource, server_factory_context);
  auto flusher = std::make_shared<OtlpMetricsFlusherImpl>(options);
  auto exporter = std::make_shared<DummyExporter>();

  OpenTelemetrySink sink(flusher, exporter, 0);

  for (auto _ : state) {
    sink.flush(*setup.snapshot);
  }
}
BENCHMARK(BM_OpenTelemetrySinkFlush_TrafficSplit_200);

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
