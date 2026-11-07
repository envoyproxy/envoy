// This file benchmarks the OpenTelemetry stats sink using the `Stats::Sink::flush` API.
// The benchmark runs at a scale of 9,000 stats (3,000 counters, 3,000 gauges, 3,000 histograms)
// with 8 tags each to test end-to-end aggregation and RPC generation performance
// under realistic load with high cardinality.

#include "envoy/stats/tag.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/histogram_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/benchmark/main.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
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

// Custom metric conversions whose matcher matches no stat, so that the stats are aggregated as
// they are.
constexpr absl::string_view NoMatchConversions = R"pb(
  matcher_list {
    matchers {
      predicate {
        single_predicate {
          input {
            name: "stat_full_name_match_input"
            typed_config {
              [type.googleapis.com/
               envoy.extensions.matching.common_inputs.stats.v3.StatFullNameMatchInput] {}
            }
          }
          value_match { exact: "no_such_stat" }
        }
      }
      on_match {
        action {
          name: "otlp_metric_drop"
          typed_config {
            [type.googleapis.com/
             envoy.extensions.stat_sinks.open_telemetry.v3.SinkConfig.DropAction] {}
          }
        }
      }
    }
  })pb";

// The same matcher, with a conversion of the stats that it doesn't match, which keeps their names
// and reports them under the resource of the pod that their `pod` tag names, in a static cluster
// and region.
constexpr absl::string_view NoMatchResourceConversion = R"pb(
  on_no_match {
    action {
      name: "otlp_metric_conversion"
      typed_config {
        [type.googleapis.com/
         envoy.extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction] {
          resource_conversion {
            static_resource_attributes {
              key: "cloud.region"
              value { string_value: "us-east1" }
            }
            static_resource_attributes {
              key: "k8s.cluster.name"
              value { string_value: "cluster" }
            }
            tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "k8s.pod.name" }
          }
        }
      }
    }
  })pb";

class DummyExporter : public OtlpMetricsExporter {
public:
  void send(MetricsExportRequestPtr&&) override {}
};

struct BenchmarkSetup {
  // If `num_pods` is not 0, each stat also has a `pod` tag with one of `num_pods` values.
  explicit BenchmarkSetup(uint32_t num_pods = 0, int num_stats_per_type = 3000) {
    snapshot = std::make_unique<testing::NiceMock<Stats::MockMetricSnapshot>>();
    hist = hist_alloc();
    hist_insert(hist, 1.5, 10);
    hist_insert(hist, 2.5, 20);
    hist_stats = std::make_unique<Stats::HistogramStatisticsImpl>(hist);

    const int num_counters = num_stats_per_type;
    const int num_gauges = num_stats_per_type;
    const int num_histograms = num_stats_per_type;

    counters.reserve(num_counters);
    gauges.reserve(num_gauges);
    histograms.reserve(num_histograms);

    auto make_tags = [num_pods](int i) {
      std::vector<Stats::Tag> tags = test_tags;
      tags.back().value_ = std::to_string(i);
      if (num_pods != 0) {
        tags.push_back({"pod", absl::StrCat("pod-", i % num_pods)});
      }
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

// Returns the number of stats of each type for the benchmarks of pod stats. Unit test runs use
// fewer stats, since destroying the stat mocks takes time quadratic in their number.
int numStatsPerType() { return Envoy::benchmark::skipExpensiveBenchmarks() ? 100 : 3000; }

// Returns a resource with the attributes that resource detectors typically detect.
Tracers::OpenTelemetry::Resource detectedResource() {
  Tracers::OpenTelemetry::Resource resource;
  resource.attributes_["cloud.provider"] = "gcp";
  resource.attributes_["cloud.region"] = "us-east1";
  resource.attributes_["k8s.cluster.name"] = "cluster";
  resource.attributes_["k8s.node.name"] = "node";
  resource.attributes_["service.name"] = "envoy";
  return resource;
}

void flushSink(::benchmark::State& state, const BenchmarkSetup& setup,
               const SinkConfig& sink_config, const Tracers::OpenTelemetry::Resource& resource) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  auto options = OtlpOptions::create(sink_config, resource, server_factory_context).value();
  auto flusher = std::make_shared<OtlpMetricsFlusherImpl>(options);
  auto exporter = std::make_shared<DummyExporter>();

  OpenTelemetrySink sink(flusher, exporter, 0);

  for (auto _ : state) {
    sink.flush(*setup.snapshot);
  }
}

void bmOpenTelemetrySinkFlushNoLimit(::benchmark::State& state) {
  BenchmarkSetup setup;
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  flushSink(state, setup, sink_config, Tracers::OpenTelemetry::Resource());
}
BENCHMARK(bmOpenTelemetrySinkFlushNoLimit);

void bmOpenTelemetrySinkFlushTrafficSplit200(::benchmark::State& state) {
  BenchmarkSetup setup;
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.set_max_data_points_per_request(200);
  flushSink(state, setup, sink_config, Tracers::OpenTelemetry::Resource());
}
BENCHMARK(bmOpenTelemetrySinkFlushTrafficSplit200);

// The benchmarks below compare sink configurations on the same stats: each stat also has a `pod`
// tag with one of state.range(0) values, and the sink resource has the attributes that resource
// detectors typically detect. state.range(1) is the maximum number of data points per request, or
// 0 for no limit.

// Flushes the pod stats to a sink with the given custom metric conversions, if any.
void flushPodStats(::benchmark::State& state, absl::string_view custom_metric_conversions) {
  BenchmarkSetup setup(state.range(0), numStatsPerType());
  SinkConfig sink_config;
  sink_config.set_max_data_points_per_request(state.range(1));
  if (!custom_metric_conversions.empty()) {
    RELEASE_ASSERT(
        Protobuf::TextFormat::ParseFromString(std::string(custom_metric_conversions),
                                              sink_config.mutable_custom_metric_conversions()),
        "");
  }
  flushSink(state, setup, sink_config, detectedResource());
}

// No custom metric conversions: each stat is streamed as a metric, without aggregation, under
// the sink resource.
void bmOpenTelemetrySinkFlushNoAggregation(::benchmark::State& state) { flushPodStats(state, ""); }
BENCHMARK(bmOpenTelemetrySinkFlushNoAggregation)
    ->ArgNames({"pods", "max_dp"})
    ->ArgsProduct({{100}, {0, 200}});

// Custom metric conversions that match no stat: the conversion matcher is evaluated for each
// stat, and the metrics are aggregated under the sink resource.
void bmOpenTelemetrySinkFlushConversions(::benchmark::State& state) {
  flushPodStats(state, NoMatchConversions);
}
BENCHMARK(bmOpenTelemetrySinkFlushConversions)
    ->ArgNames({"pods", "max_dp"})
    ->ArgsProduct({{100}, {0, 200}});

// The same, but the stats that the matcher doesn't match are converted under the resource of
// their pod: compared to bmOpenTelemetrySinkFlushConversions, measures the cost of resolving the
// resources of the metrics and of reporting them under several resources.
void bmOpenTelemetrySinkFlushResourceConversion(::benchmark::State& state) {
  flushPodStats(state, absl::StrCat(NoMatchConversions, NoMatchResourceConversion));
}
BENCHMARK(bmOpenTelemetrySinkFlushResourceConversion)
    ->ArgNames({"pods", "max_dp"})
    ->ArgsProduct({{1, 100, 3000}, {0, 200}});

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
