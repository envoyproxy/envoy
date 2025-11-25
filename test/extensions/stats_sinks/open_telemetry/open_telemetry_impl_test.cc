#include "envoy/grpc/async_client.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {
namespace {

class OpenTelemetryStatsSinkTests : public testing::Test {
public:
  OpenTelemetryStatsSinkTests() {
    time_t expected_time = 1212;
    expected_time_ns_ = expected_time * 1000000000;
    SystemTime time = std::chrono::system_clock::from_time_t(expected_time);
    EXPECT_CALL(snapshot_, snapshotTime()).WillRepeatedly(Return(time));
  }

  ~OpenTelemetryStatsSinkTests() override {
    for (histogram_t* hist : histogram_ptrs_) {
      hist_free(hist);
    }
  }

  const OtlpOptionsSharedPtr
  otlpOptions(bool report_counters_as_deltas = false, bool report_histograms_as_deltas = false,
              bool emit_tags_as_attributes = true, bool use_tag_extracted_name = true,
              const std::string& stat_prefix = "",
              absl::flat_hash_map<std::string, std::string> resource_attributes = {},
              absl::string_view metric_conversion_pbtext = "") {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.set_report_counters_as_deltas(report_counters_as_deltas);
    sink_config.set_report_histograms_as_deltas(report_histograms_as_deltas);
    sink_config.mutable_emit_tags_as_attributes()->set_value(emit_tags_as_attributes);
    sink_config.mutable_use_tag_extracted_name()->set_value(use_tag_extracted_name);
    sink_config.set_prefix(stat_prefix);
    Tracers::OpenTelemetry::Resource resource;
    for (const auto& [key, value] : resource_attributes) {
      resource.attributes_[key] = value;
    }
    if (!metric_conversion_pbtext.empty()) {
      Protobuf::TextFormat::ParseFromString(metric_conversion_pbtext,
                                            sink_config.mutable_custom_metric_conversions());
    }
    return std::make_shared<OtlpOptions>(sink_config, resource, server_factory_context_);
  }

  std::string getTagExtractedName(const std::string name) { return name + "-tagged"; }

  void addCounterToSnapshot(const std::string& name, uint64_t delta, uint64_t value,
                            bool used = true,
                            const Stats::TagVector& tags = {{"counter_key", "counter_val"}}) {
    counter_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockCounter>>());
    counter_storage_.back()->name_ = name;
    counter_storage_.back()->setTagExtractedName(getTagExtractedName(name));
    counter_storage_.back()->value_ = value;
    counter_storage_.back()->used_ = used;
    counter_storage_.back()->setTags({{"counter_key", "counter_val"}});
    counter_storage_.back()->setTags(tags);
    snapshot_.counters_.push_back({delta, *counter_storage_.back()});
  }

  void addHostCounterToSnapshot(const std::string& name, uint64_t delta, uint64_t value) {
    Stats::PrimitiveCounter counter;
    counter.add(value - delta);
    counter.latch();
    counter.add(delta);
    Stats::PrimitiveCounterSnapshot s(counter);
    s.setName(std::string(name));
    s.setTagExtractedName(getTagExtractedName(name));
    s.setTags({{"counter_key", "counter_val"}});
    snapshot_.host_counters_.push_back(s);
  }

  void addGaugeToSnapshot(const std::string& name, uint64_t value, bool used = true,
                          const Stats::TagVector& tags = {{"gauge_key", "gauge_val"}}) {
    gauge_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockGauge>>());
    gauge_storage_.back()->name_ = name;
    gauge_storage_.back()->setTagExtractedName(getTagExtractedName(name));
    gauge_storage_.back()->value_ = value;
    gauge_storage_.back()->used_ = used;
    gauge_storage_.back()->setTags(tags);

    snapshot_.gauges_.push_back(*gauge_storage_.back());
  }

  void addHostGaugeToSnapshot(const std::string& name, uint64_t value) {
    Stats::PrimitiveGauge gauge;
    gauge.add(value);
    Stats::PrimitiveGaugeSnapshot s(gauge);
    s.setName(std::string(name));
    s.setTagExtractedName(getTagExtractedName(name));
    s.setTags({{"gauge_key", "gauge_val"}});
    snapshot_.host_gauges_.push_back(s);
  }

  void addHistogramToSnapshot(const std::string& name, bool is_delta = false, bool used = true,
                              const Stats::TagVector& tags = {{"hist_key", "hist_val"}}) {
    auto histogram = std::make_unique<NiceMock<Stats::MockParentHistogram>>();

    histogram_t* hist = hist_alloc();

    // Using the default histogram boundaries for testing. For delta histogram,
    // it's expected that even indexes will have count of 1, and 0 otherwise.
    // For cumulative histogram, it's expected that odd indexes will have count
    // of 1, and 0 otherwise.
    std::vector<double> values;
    if (is_delta) {
      values = {0.2, 3, 16, 75, 400, 2000, 7500, 50000, 500000, 3000000};
    } else {
      // The last value will be used to test a scenario of a bucket which is
      // outside the defined histogram bounds.
      values = {0.7, 7, 35, 200, 750, 4000, 20000, 200000, 1500000, 4000000};
    }

    for (auto value : values) {
      hist_insert(hist, value, 1);
    }

    histogram_ptrs_.push_back(hist);
    hist_stats_.push_back(std::make_unique<Stats::HistogramStatisticsImpl>(hist));

    if (is_delta) {
      ON_CALL(*histogram, intervalStatistics()).WillByDefault(ReturnRef(*hist_stats_.back()));
    } else {
      ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(*hist_stats_.back()));
    }

    histogram_storage_.emplace_back(std::move(histogram));
    histogram_storage_.back()->name_ = name;
    histogram_storage_.back()->setTagExtractedName(getTagExtractedName(name));
    histogram_storage_.back()->used_ = used;
    histogram_storage_.back()->setTags(tags);

    snapshot_.histograms_.push_back(*histogram_storage_.back());
  }

  long long int expected_time_ns_;
  std::vector<histogram_t*> histogram_ptrs_;
  std::vector<std::unique_ptr<Stats::HistogramStatisticsImpl>> hist_stats_;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockCounter>>> counter_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockGauge>>> gauge_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockParentHistogram>>> histogram_storage_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
};

class OpenTelemetryGrpcMetricsExporterImplTest : public OpenTelemetryStatsSinkTests {
public:
  OpenTelemetryGrpcMetricsExporterImplTest() {
    exporter_ = std::make_unique<OpenTelemetryGrpcMetricsExporterImpl>(
        otlpOptions(), Grpc::RawAsyncClientSharedPtr{async_client_});
  }

  Grpc::MockAsyncClient* async_client_{new NiceMock<Grpc::MockAsyncClient>};
  OpenTelemetryGrpcMetricsExporterImplPtr exporter_;
};

TEST_F(OpenTelemetryGrpcMetricsExporterImplTest, SendExportRequest) {
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _));
  exporter_->send(std::make_unique<MetricsExportRequest>());
}

TEST_F(OpenTelemetryGrpcMetricsExporterImplTest, PartialSuccess) {
  auto response = std::make_unique<MetricsExportResponse>();
  response->mutable_partial_success()->set_rejected_data_points(1);
  exporter_->onSuccess(std::move(response), Tracing::NullSpan::instance());
}

class OtlpMetricsFlusherTests : public OpenTelemetryStatsSinkTests {
public:
  void expectMetricsCount(MetricsExportRequestSharedPtr& request, int count) {
    EXPECT_EQ(1, request->resource_metrics().size());
    EXPECT_EQ(1, request->resource_metrics()[0].scope_metrics().size());
    EXPECT_EQ(count, request->resource_metrics()[0].scope_metrics()[0].metrics().size());
  }

  const opentelemetry::proto::metrics::v1::Metric& metricAt(int index,
                                                            MetricsExportRequestSharedPtr metrics) {
    return metrics->resource_metrics()[0].scope_metrics()[0].metrics()[index];
  }

  // Helper to sort metrics by name and then by attributes
  void sortMetrics(Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>& metrics) {
    std::sort(
        metrics.begin(), metrics.end(),
        [](const opentelemetry::proto::metrics::v1::Metric& a,
           const opentelemetry::proto::metrics::v1::Metric& b) {
          if (a.name() != b.name()) {
            return a.name() < b.name();
          }
          // Secondary sort by attributes
          auto get_attributes_str = [](const opentelemetry::proto::metrics::v1::Metric& metric) {
            const Protobuf::RepeatedPtrField<KeyValue>* attributes = nullptr;
            if (metric.has_sum() && metric.sum().data_points_size() > 0) {
              attributes = &metric.sum().data_points()[0].attributes();
            } else if (metric.has_gauge() && metric.gauge().data_points_size() > 0) {
              attributes = &metric.gauge().data_points()[0].attributes();
            } else if (metric.has_histogram() && metric.histogram().data_points_size() > 0) {
              attributes = &metric.histogram().data_points()[0].attributes();
            }
            if (attributes == nullptr)
              return std::string("");
            std::vector<std::pair<std::string, std::string>> attrs;
            for (const auto& attr : *attributes) {
              attrs.push_back({attr.key(), attr.value().string_value()});
            }
            std::sort(attrs.begin(), attrs.end());
            std::string attr_str;
            for (const auto& attr : attrs) {
              attr_str += attr.first + "=" + attr.second + ";";
            }
            return attr_str;
          };
          return get_attributes_str(a) < get_attributes_str(b);
        });
  }

  const opentelemetry::proto::metrics::v1::Metric* findMetric(MetricsExportRequestSharedPtr metrics,
                                                              const std::string& name) {
    for (const auto& metric : metrics->resource_metrics()[0].scope_metrics()[0].metrics()) {
      if (metric.name() == name) {
        return &metric;
      }
    }
    return nullptr;
  }

  const opentelemetry::proto::metrics::v1::Metric* findGauge(MetricsExportRequestSharedPtr metrics,
                                                             const std::string& name) {
    const auto* metric = findMetric(metrics, name);
    EXPECT_NE(metric, nullptr) << "Gauge metric '" << name << "' not found.";
    if (metric == nullptr) {
      return nullptr;
    }
    EXPECT_TRUE(metric->has_gauge());
    return metric;
  }

  const opentelemetry::proto::metrics::v1::Metric* findSum(MetricsExportRequestSharedPtr metrics,
                                                           const std::string& name) {
    const auto* metric = findMetric(metrics, name);
    EXPECT_NE(metric, nullptr) << "Sum metric '" << name << "' not found.";
    if (metric == nullptr) {
      return nullptr;
    }
    EXPECT_TRUE(metric->has_sum());
    return metric;
  }

  const opentelemetry::proto::metrics::v1::Metric*
  findHistogram(MetricsExportRequestSharedPtr metrics, const std::string& name) {
    const auto* metric = findMetric(metrics, name);
    EXPECT_NE(metric, nullptr) << "Histogram metric '" << name << "' not found.";
    if (metric == nullptr) {
      return nullptr;
    }
    EXPECT_TRUE(metric->has_histogram());
    return metric;
  }

  void expectGauge(const opentelemetry::proto::metrics::v1::Metric& metric, std::string name,
                   int value) {
    EXPECT_EQ(name, metric.name());
    EXPECT_TRUE(metric.has_gauge());
    EXPECT_EQ(1, metric.gauge().data_points().size());
    EXPECT_EQ(value, metric.gauge().data_points()[0].as_int());
    EXPECT_EQ(expected_time_ns_, metric.gauge().data_points()[0].time_unix_nano());
    EXPECT_EQ(0, metric.gauge().data_points()[0].start_time_unix_nano());
  }

  void expectSum(const opentelemetry::proto::metrics::v1::Metric& metric, std::string name,
                 int value, bool is_delta) {
    EXPECT_EQ(name, metric.name());
    EXPECT_TRUE(metric.has_sum());
    EXPECT_TRUE(metric.sum().is_monotonic());

    if (is_delta) {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA,
                metric.sum().aggregation_temporality());
      EXPECT_EQ(delta_start_time_ns_, metric.sum().data_points()[0].start_time_unix_nano());
    } else {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                metric.sum().aggregation_temporality());
      EXPECT_EQ(cumulative_start_time_ns_, metric.sum().data_points()[0].start_time_unix_nano());
    }

    EXPECT_EQ(1, metric.sum().data_points().size());
    EXPECT_EQ(value, metric.sum().data_points()[0].as_int());
    EXPECT_EQ(expected_time_ns_, metric.sum().data_points()[0].time_unix_nano());
  }

  void expectHistogram(const opentelemetry::proto::metrics::v1::Metric& metric, std::string name,
                       bool is_delta) {
    EXPECT_EQ(name, metric.name());
    EXPECT_TRUE(metric.has_histogram());
    EXPECT_EQ(1, metric.histogram().data_points().size());

    auto data_point = metric.histogram().data_points()[0];
    EXPECT_EQ(10, data_point.count());

    const int default_buckets_count = 19;
    // The buckets count needs to be one element bigger than the bounds size.
    EXPECT_EQ(default_buckets_count + 1, data_point.bucket_counts().size());
    EXPECT_EQ(default_buckets_count, data_point.explicit_bounds().size());
    EXPECT_EQ(expected_time_ns_, data_point.time_unix_nano());

    if (is_delta) {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA,
                metric.histogram().aggregation_temporality());
      EXPECT_EQ(delta_start_time_ns_, data_point.start_time_unix_nano());
      EXPECT_GE(data_point.sum(), 3559994.2);
    } else {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                metric.histogram().aggregation_temporality());
      EXPECT_EQ(cumulative_start_time_ns_, data_point.start_time_unix_nano());
      EXPECT_GE(data_point.sum(), 5724992.7);
    }

    for (int idx = 0; idx < data_point.bucket_counts().size(); idx++) {
      int expected_value = (is_delta) ? (1 - (idx % 2)) : (idx % 2);
      EXPECT_EQ(expected_value, data_point.bucket_counts()[idx]);
    }
  }

  void expectNoAttributes(const Protobuf::RepeatedPtrField<KeyValue>& attributes) {
    EXPECT_EQ(0, attributes.size());
  }

  void expectAttributes(const Protobuf::RepeatedPtrField<KeyValue>& attributes, std::string key,
                        std::string value) {
    EXPECT_EQ(1, attributes.size());
    EXPECT_EQ(key, attributes[0].key());
    EXPECT_EQ(value, attributes[0].value().string_value());
  }

  int64_t delta_start_time_ns_ = 123;
  int64_t cumulative_start_time_ns_ = 456;
};

TEST_F(OtlpMetricsFlusherTests, MetricsWithDefaultOptions) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addCounterToSnapshot("test_counter", 1, 1);
  addHostCounterToSnapshot("test_host_counter", 2, 3);
  addGaugeToSnapshot("test_gauge", 1);
  addHostGaugeToSnapshot("test_host_gauge", 4);
  addHistogramToSnapshot("test_histogram");

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 5);

  {
    const auto* metric = findGauge(metrics, getTagExtractedName("test_gauge"));
    ASSERT_NE(metric, nullptr);
    expectGauge(*metric, getTagExtractedName("test_gauge"), 1);
    expectAttributes(metric->gauge().data_points()[0].attributes(), "gauge_key", "gauge_val");
  }

  {
    const auto* metric = findGauge(metrics, getTagExtractedName("test_host_gauge"));
    ASSERT_NE(metric, nullptr);
    expectGauge(*metric, getTagExtractedName("test_host_gauge"), 4);
    expectAttributes(metric->gauge().data_points()[0].attributes(), "gauge_key", "gauge_val");
  }

  {
    const auto* metric = findSum(metrics, getTagExtractedName("test_counter"));
    ASSERT_NE(metric, nullptr);
    expectSum(*metric, getTagExtractedName("test_counter"), 1, false);
    expectAttributes(metric->sum().data_points()[0].attributes(), "counter_key", "counter_val");
  }

  {
    const auto* metric = findSum(metrics, getTagExtractedName("test_host_counter"));
    ASSERT_NE(metric, nullptr);
    expectSum(*metric, getTagExtractedName("test_host_counter"), 3, false);
    expectAttributes(metric->sum().data_points()[0].attributes(), "counter_key", "counter_val");
  }

  {
    const auto* metric = findHistogram(metrics, getTagExtractedName("test_histogram"));
    ASSERT_NE(metric, nullptr);
    expectHistogram(*metric, getTagExtractedName("test_histogram"), false);
    expectAttributes(metric->histogram().data_points()[0].attributes(), "hist_key", "hist_val");
  }

  gauge_storage_.back()->used_ = false;
  metrics = flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 4);
}

TEST_F(OtlpMetricsFlusherTests, MetricsWithStatsPrefix) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "prefix"));

  addCounterToSnapshot("test_counter", 1, 1);
  addHostCounterToSnapshot("test_host_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addGaugeToSnapshot("test_host_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 5);

  {
    const auto* metric = findGauge(metrics, getTagExtractedName("prefix.test_gauge"));
    ASSERT_NE(metric, nullptr);
    expectGauge(*metric, getTagExtractedName("prefix.test_gauge"), 1);
  }
  {
    const auto* metric = findGauge(metrics, getTagExtractedName("prefix.test_host_gauge"));
    ASSERT_NE(metric, nullptr);
    expectGauge(*metric, getTagExtractedName("prefix.test_host_gauge"), 1);
  }
  {
    const auto* metric = findSum(metrics, getTagExtractedName("prefix.test_counter"));
    ASSERT_NE(metric, nullptr);
    expectSum(*metric, getTagExtractedName("prefix.test_counter"), 1, false);
  }
  {
    const auto* metric = findSum(metrics, getTagExtractedName("prefix.test_host_counter"));
    ASSERT_NE(metric, nullptr);
    expectSum(*metric, getTagExtractedName("prefix.test_host_counter"), 1, false);
  }
  {
    const auto* metric = findHistogram(metrics, getTagExtractedName("prefix.test_histogram"));
    ASSERT_NE(metric, nullptr);
    expectHistogram(*metric, getTagExtractedName("prefix.test_histogram"), false);
  }
}

TEST_F(OtlpMetricsFlusherTests, MetricsWithNoTaggedName) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, false));

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 3);

  {
    const auto* metric = findGauge(metrics, "test_gauge");
    ASSERT_NE(metric, nullptr);
    expectGauge(*metric, "test_gauge", 1);
  }
  {
    const auto* metric = findSum(metrics, "test_counter");
    ASSERT_NE(metric, nullptr);
    expectSum(*metric, "test_counter", 1, false);
  }
  {
    const auto* metric = findHistogram(metrics, "test_histogram");
    ASSERT_NE(metric, nullptr);
    expectHistogram(*metric, "test_histogram", false);
  }
}

TEST_F(OtlpMetricsFlusherTests, MetricsWithNoAttributes) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, false, true));

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");
  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 3);
  {
    const auto* metric = findGauge(metrics, getTagExtractedName("test_gauge"));
    ASSERT_NE(metric, nullptr);
    expectGauge(*metric, getTagExtractedName("test_gauge"), 1);
    expectNoAttributes(metric->gauge().data_points()[0].attributes());
  }

  {
    const auto* metric = findSum(metrics, getTagExtractedName("test_counter"));
    ASSERT_NE(metric, nullptr);
    expectSum(*metric, getTagExtractedName("test_counter"), 1, false);
    expectNoAttributes(metric->sum().data_points()[0].attributes());
  }

  {
    const auto* metric = findHistogram(metrics, getTagExtractedName("test_histogram"));
    ASSERT_NE(metric, nullptr);
    expectHistogram(*metric, getTagExtractedName("test_histogram"), false);
    expectNoAttributes(metric->histogram().data_points()[0].attributes());
  }
}

TEST_F(OtlpMetricsFlusherTests, GaugeMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addGaugeToSnapshot("test_gauge1", 1);
  addGaugeToSnapshot("test_gauge2", 2);
  addHostGaugeToSnapshot("test_host_gauge1", 3);
  addHostGaugeToSnapshot("test_host_gauge2", 4);

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 4);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_gauge1")),
              getTagExtractedName("test_gauge1"), 1);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_gauge2")),
              getTagExtractedName("test_gauge2"), 2);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_host_gauge1")),
              getTagExtractedName("test_host_gauge1"), 3);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_host_gauge2")),
              getTagExtractedName("test_host_gauge2"), 4);
}

TEST_F(OtlpMetricsFlusherTests, CumulativeCounterMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addCounterToSnapshot("test_counter1", 1, 1);
  addCounterToSnapshot("test_counter2", 2, 3);
  addHostCounterToSnapshot("test_host_counter1", 2, 4);
  addHostCounterToSnapshot("test_host_counter2", 5, 10);

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 4);
  expectSum(*findSum(metrics, getTagExtractedName("test_counter1")),
            getTagExtractedName("test_counter1"), 1, false);
  expectSum(*findSum(metrics, getTagExtractedName("test_counter2")),
            getTagExtractedName("test_counter2"), 3, false);
  expectSum(*findSum(metrics, getTagExtractedName("test_host_counter1")),
            getTagExtractedName("test_host_counter1"), 4, false);
  expectSum(*findSum(metrics, getTagExtractedName("test_host_counter2")),
            getTagExtractedName("test_host_counter2"), 10, false);
}

TEST_F(OtlpMetricsFlusherTests, DeltaCounterMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(true, false, true, true));

  addCounterToSnapshot("test_counter1", 1, 1);
  addCounterToSnapshot("test_counter2", 2, 3);
  addHostCounterToSnapshot("test_host_counter1", 2, 4);
  addHostCounterToSnapshot("test_host_counter2", 5, 10);

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);

  expectMetricsCount(metrics, 4);
  expectSum(*findSum(metrics, getTagExtractedName("test_counter1")),
            getTagExtractedName("test_counter1"), 1, true);
  expectSum(*findSum(metrics, getTagExtractedName("test_counter2")),
            getTagExtractedName("test_counter2"), 2, true);
  expectSum(*findSum(metrics, getTagExtractedName("test_host_counter1")),
            getTagExtractedName("test_host_counter1"), 2, true);
  expectSum(*findSum(metrics, getTagExtractedName("test_host_counter2")),
            getTagExtractedName("test_host_counter2"), 5, true);
}

TEST_F(OtlpMetricsFlusherTests, CumulativeHistogramMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addHistogramToSnapshot("test_histogram1");
  addHistogramToSnapshot("test_histogram2");

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 2);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram1")),
                  getTagExtractedName("test_histogram1"), false);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram2")),
                  getTagExtractedName("test_histogram2"), false);
}

TEST_F(OtlpMetricsFlusherTests, DeltaHistogramMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, true, true, true));

  addHistogramToSnapshot("test_histogram1", true);
  addHistogramToSnapshot("test_histogram2", true);

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 2);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram1")),
                  getTagExtractedName("test_histogram1"), true);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram2")),
                  getTagExtractedName("test_histogram2"), true);
}

using OtlpMetricsFlusherAggregationTests = OtlpMetricsFlusherTests;

TEST_F(OtlpMetricsFlusherAggregationTests, MetricsWithLabelsAggregationCounter) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "prefix", {},
                                             R"pb( matcher_list {
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
                   value_match { safe_regex { regex: "test_counter-1" } }
                 }
               }
               on_match {
                 action {
                   name: "otlp_metric_conversion"
                   typed_config {
                     [type.googleapis.com/envoy.extensions.stat_sinks
                          .open_telemetry.v3.SinkConfig.ConversionAction] {
                       metric_name: "new_counter_name"
                     }
                   }
                 }
               }
             }
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
                   value_match { safe_regex { regex: "test_counter-." } }
                 }
               }
               on_match {
                 action {
                   name: "otlp_metric_conversion"
                   typed_config {
                     [type.googleapis.com/envoy.extensions.stat_sinks
                          .open_telemetry.v3.SinkConfig.ConversionAction] {
                       metric_name: "new_counter_name"
                     }
                   }
                 }
               }
             }
           })pb"));
  // Add counters with same name, different tags
  addCounterToSnapshot("test_counter-1", 0, 1, true, {{"key", "val1"}});
  addCounterToSnapshot("test_counter-2", 0, 99, true, {{"key", "val1"}});
  addCounterToSnapshot("test_counter-1", 0, 3, true, {{"key", "val2"}});
  // Add unconverted metrics with the same name but different tags
  addCounterToSnapshot("unmapped_counter", 0, 1, true, {{"keyX", "valX"}});
  addCounterToSnapshot("unmapped_counter", 0, 5, true, {{"keyY", "valY"}});

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  const int expected_metrics_count = 2;
  expectMetricsCount(metrics, expected_metrics_count);

  auto& exported_metrics =
      const_cast<Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>&>(
          metrics->resource_metrics()[0].scope_metrics()[0].metrics());
  sortMetrics(exported_metrics);

  // Counter: new_counter_name (remapped)
  const auto& metric = exported_metrics[0];
  EXPECT_EQ("new_counter_name", metric.name());
  EXPECT_TRUE(metric.has_sum());
  EXPECT_EQ(2, metric.sum().data_points().size());
  // Data Point 1: {"key": "val1"}
  EXPECT_EQ(100, metric.sum().data_points()[0].as_int()); // 1 + 99
  expectAttributes(metric.sum().data_points()[0].attributes(), "key", "val1");
  // Data Point 2: {"key": "val2"}
  EXPECT_EQ(3, metric.sum().data_points()[1].as_int());
  expectAttributes(metric.sum().data_points()[1].attributes(), "key", "val2");

  // Counter: prefix.unmapped_counter-tagged (unmapped)
  const auto& unmapped_metric = exported_metrics[1];
  EXPECT_EQ(getTagExtractedName("prefix.unmapped_counter"), unmapped_metric.name());
  EXPECT_TRUE(unmapped_metric.has_sum());
  EXPECT_EQ(2, unmapped_metric.sum().data_points().size());
  // data point 1: keyX: valX
  EXPECT_EQ(1, unmapped_metric.sum().data_points()[0].as_int());
  expectAttributes(unmapped_metric.sum().data_points()[0].attributes(), "keyX", "valX");
  // data point 2: keyY: valY
  EXPECT_EQ(5, unmapped_metric.sum().data_points()[1].as_int());
  expectAttributes(unmapped_metric.sum().data_points()[1].attributes(), "keyY", "valY");
}

TEST_F(OtlpMetricsFlusherAggregationTests, MetricsWithLabelsAggregationGauge) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "prefix", {},
                                             R"pb( matcher_list {
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
                   value_match { safe_regex { regex: "test_gauge-1" } }
                 }
               }
               on_match {
                 action {
                   name: "otlp_metric_conversion"
                   typed_config {
                     [type.googleapis.com/envoy.extensions.stat_sinks
                          .open_telemetry.v3.SinkConfig.ConversionAction] {
                       metric_name: "new_gauge_name"
                     }
                   }
                 }
               }
             }
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
                   value_match { safe_regex { regex: "test_gauge-." } }
                 }
               }
               on_match {
                 action {
                   name: "otlp_metric_conversion"
                   typed_config {
                     [type.googleapis.com/envoy.extensions.stat_sinks
                          .open_telemetry.v3.SinkConfig.ConversionAction] {
                       metric_name: "new_gauge_name"
                     }
                   }
                 }
               }
             }
           })pb"));
  // Add gauges with same name, different tags
  addGaugeToSnapshot("test_gauge-1", 1, true, {{"key", "valA"}});
  addGaugeToSnapshot("test_gauge-2", 2, true, {{"key", "valA"}});
  addGaugeToSnapshot("test_gauge-1", 3, true, {{"key", "valB"}});
  // Add unconverted metrics with the same name but different tags
  addGaugeToSnapshot("unmapped_gauge", 4, true, {{"keyX", "valX"}});
  addGaugeToSnapshot("unmapped_gauge", 10, true, {{"keyY", "valY"}});

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  const int expected_metrics_count = 2;
  expectMetricsCount(metrics, expected_metrics_count);

  auto& exported_metrics =
      const_cast<Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>&>(
          metrics->resource_metrics()[0].scope_metrics()[0].metrics());
  sortMetrics(exported_metrics);
  // Gauge: new_gauge_name (remapped)
  const auto& metric = exported_metrics[0];
  EXPECT_EQ("new_gauge_name", metric.name());
  EXPECT_TRUE(metric.has_gauge());
  EXPECT_EQ(2, metric.gauge().data_points().size());
  // Data Point 1: {"key": "valA"} - Aggregated from test_gauge-1 and
  // test_gauge-2
  EXPECT_EQ(3, metric.gauge().data_points()[0].as_int()); // 1 + 2
  expectAttributes(metric.gauge().data_points()[0].attributes(), "key", "valA");
  // Data Point 2: {"key": "valB"} - From test_gauge-1
  EXPECT_EQ(3, metric.gauge().data_points()[1].as_int());
  expectAttributes(metric.gauge().data_points()[1].attributes(), "key", "valB");
  // Gauge: prefix.unmapped_gauge-tagged (unmapped)
  const auto& unmapped_metric = exported_metrics[1];
  EXPECT_EQ(getTagExtractedName("prefix.unmapped_gauge"), unmapped_metric.name());
  EXPECT_TRUE(unmapped_metric.has_gauge());
  EXPECT_EQ(2, unmapped_metric.gauge().data_points().size());
  // data point 1: keyX: valX
  EXPECT_EQ(4, unmapped_metric.gauge().data_points()[0].as_int());
  expectAttributes(unmapped_metric.gauge().data_points()[0].attributes(), "keyX", "valX");
  // data point 2: keyY: valY
  EXPECT_EQ(10, unmapped_metric.gauge().data_points()[1].as_int());
  expectAttributes(unmapped_metric.gauge().data_points()[1].attributes(), "keyY", "valY");
}

TEST_F(OtlpMetricsFlusherAggregationTests, MetricsWithLabelsAggregationHistogram) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "prefix", {},
                                             R"pb( matcher_list {
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
                   value_match { safe_regex { regex: "test_histogram-1" } }
                 }
               }
               on_match {
                 action {
                   name: "otlp_metric_conversion"
                   typed_config {
                     [type.googleapis.com/envoy.extensions.stat_sinks
                          .open_telemetry.v3.SinkConfig.ConversionAction] {
                       metric_name: "new_histogram_name"
                     }
                   }
                 }
               }
             }
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
                   value_match { safe_regex { regex: "test_histogram-." } }
                 }
               }
               on_match {
                 action {
                   name: "otlp_metric_conversion"
                   typed_config {
                     [type.googleapis.com/envoy.extensions.stat_sinks
                          .open_telemetry.v3.SinkConfig.ConversionAction] {
                       metric_name: "new_histogram_name"
                     }
                   }
                 }
               }
             }
           })pb"));
  // Add histograms with same name, different tags
  addHistogramToSnapshot("test_histogram-1", false, true, {{"key", "hist1"}});
  addHistogramToSnapshot("test_histogram-2", false, true, {{"key", "hist1"}});
  addHistogramToSnapshot("test_histogram-1", false, true, {{"key", "hist2"}});
  // Add unconverted metrics with the same name but different tags
  addHistogramToSnapshot("unmapped_histogram", false, true, {{"keyX", "valX"}});
  addHistogramToSnapshot("unmapped_histogram", false, true, {{"keyY", "valY"}});

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  const int expected_metrics_count = 2;
  expectMetricsCount(metrics, expected_metrics_count);

  auto& exported_metrics =
      const_cast<Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>&>(
          metrics->resource_metrics()[0].scope_metrics()[0].metrics());
  sortMetrics(exported_metrics);
  // Histogram: new_histogram_name (remapped)
  const auto& metric = exported_metrics[0];
  EXPECT_EQ("new_histogram_name", metric.name());
  EXPECT_TRUE(metric.has_histogram());
  EXPECT_EQ(2, metric.histogram().data_points().size());
  // Data Point 1: {"key": "hist1"} - Aggregated from test_histogram-1 and
  // test_histogram-2
  auto data_point1 = metric.histogram().data_points()[0];
  EXPECT_EQ(20, data_point1.count()); // Each original hist has count 10
  // The sum should be double the sum of a single cumulative histogram.
  EXPECT_NEAR(data_point1.sum(), 11661106.51, 0.1);
  expectAttributes(data_point1.attributes(), "key", "hist1");
  // Check bucket counts are doubled.
  const int default_buckets_count = 19;
  EXPECT_EQ(default_buckets_count + 1, data_point1.bucket_counts().size());
  for (int idx = 0; idx < data_point1.bucket_counts().size(); idx++) {
    int expected_value = (idx % 2) * 2; // Doubled from single cumulative hist
    EXPECT_EQ(expected_value, data_point1.bucket_counts()[idx]);
  }
  // Data Point 2: {"key": "hist2"} - From test_histogram-1
  auto data_point2 = metric.histogram().data_points()[1];
  EXPECT_EQ(10, data_point2.count());
  expectAttributes(data_point2.attributes(), "key", "hist2");

  // Histogram: prefix.unmapped_histogram-tagged (unmapped)
  const auto& unmapped_metric = exported_metrics[1];
  EXPECT_EQ(getTagExtractedName("prefix.unmapped_histogram"), unmapped_metric.name());
  EXPECT_TRUE(unmapped_metric.has_histogram());
  EXPECT_EQ(2, unmapped_metric.histogram().data_points().size());
  // data point 1: keyX: valX
  expectAttributes(unmapped_metric.histogram().data_points()[0].attributes(), "keyX", "valX");
  // data point 2: keyY: valY
  expectAttributes(unmapped_metric.histogram().data_points()[1].attributes(), "keyY", "valY");
}

TEST_F(OtlpMetricsFlusherAggregationTests, MetricsWithStaticMetricLabels) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "", {},
                                             R"pb(
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
                value_match {
                  safe_regex {
                    regex: "test_counter"
                  }
                }
              }
            }
            on_match {
              action {
                name: "otlp_metric_conversion"
                typed_config {
                  [type.googleapis.com/envoy.extensions.stat_sinks
                       .open_telemetry.v3.SinkConfig.ConversionAction] {
                    metric_name: "static_counter"
                    static_metric_labels {
                      key: "static_key_c"
                      value { string_value: "static_val_c" }
                    }
                  }
                }
              }
            }
          }
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
                value_match {
                  safe_regex {
                    regex: "test_gauge"
                  }
                }
              }
            }
            on_match {
              action {
                name: "otlp_metric_conversion"
                typed_config {
                  [type.googleapis.com/envoy.extensions.stat_sinks
                       .open_telemetry.v3.SinkConfig.ConversionAction] {
                    metric_name: "static_gauge"
                    static_metric_labels {
                      key: "static_key_g"
                      value { string_value: "static_val_g" }
                    }
                  }
                }
              }
            }
          }
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
                value_match {
                  safe_regex {
                    regex: "test_histogram"
                  }
                }
              }
            }
            on_match {
              action {
                name: "otlp_metric_conversion"
                typed_config {
                  [type.googleapis.com/envoy.extensions.stat_sinks
                       .open_telemetry.v3.SinkConfig.ConversionAction] {
                    metric_name: "static_histogram"
                    static_metric_labels {
                      key: "static_key_h"
                      value { string_value: "static_val_h" }
                    }
                  }
                }
              }
            }
          }
        }
      )pb"));

  addCounterToSnapshot("test_counter", 1, 1, true, {{"key", "val1"}});
  addGaugeToSnapshot("test_gauge", 1, true, {{"key", "valA"}});
  addHistogramToSnapshot("test_histogram", false, true, {{"key", "hist1"}});

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 3);

  auto& exported_metrics =
      const_cast<Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>&>(
          metrics->resource_metrics()[0].scope_metrics()[0].metrics());
  sortMetrics(exported_metrics);

  // Expected metrics in sorted order:
  // 0: static_counter
  // 1: static_gauge
  // 2: static_histogram

  // Counter: static_counter
  {
    const auto& metric = exported_metrics[0];
    EXPECT_EQ("static_counter", metric.name());
    EXPECT_TRUE(metric.has_sum());
    EXPECT_EQ(1, metric.sum().data_points().size());
    EXPECT_EQ(1, metric.sum().data_points()[0].as_int());
    const auto& attrs = metric.sum().data_points()[0].attributes();
    EXPECT_EQ(2, attrs.size());
    EXPECT_EQ("key", attrs[0].key());
    EXPECT_EQ("val1", attrs[0].value().string_value());
    EXPECT_EQ("static_key_c", attrs[1].key());
    EXPECT_EQ("static_val_c", attrs[1].value().string_value());
  }

  // Gauge: static_gauge
  {
    const auto& metric = exported_metrics[1];
    EXPECT_EQ("static_gauge", metric.name());
    EXPECT_TRUE(metric.has_gauge());
    EXPECT_EQ(1, metric.gauge().data_points().size());
    EXPECT_EQ(1, metric.gauge().data_points()[0].as_int());
    const auto& attrs = metric.gauge().data_points()[0].attributes();
    EXPECT_EQ(2, attrs.size());
    EXPECT_EQ("key", attrs[0].key());
    EXPECT_EQ("valA", attrs[0].value().string_value());
    EXPECT_EQ("static_key_g", attrs[1].key());
    EXPECT_EQ("static_val_g", attrs[1].value().string_value());
  }

  // Histogram: static_histogram
  {
    const auto& metric = exported_metrics[2];
    EXPECT_EQ("static_histogram", metric.name());
    EXPECT_TRUE(metric.has_histogram());
    EXPECT_EQ(1, metric.histogram().data_points().size());
    const auto& attrs = metric.histogram().data_points()[0].attributes();
    EXPECT_EQ(2, attrs.size());
    EXPECT_EQ("key", attrs[0].key());
    EXPECT_EQ("hist1", attrs[0].value().string_value());
    EXPECT_EQ("static_key_h", attrs[1].key());
    EXPECT_EQ("static_val_h", attrs[1].value().string_value());
  }
}

TEST_F(OtlpMetricsFlusherTests, DropMetrics) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "", {},
                                             R"pb(
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
                value_match {
                  safe_regex {
                    regex: ".*drop.*"
                  }
                }
              }
            }
            on_match {
              action {
                name: "otlp_metric_drop_action"
                typed_config {
                  [type.googleapis.com/envoy.extensions.stat_sinks
                       .open_telemetry.v3.SinkConfig.DropAction] {}
                }
              }
            }
          }
        }
      )pb"));

  addCounterToSnapshot("test_counter", 1, 1);
  addCounterToSnapshot("drop_counter", 1, 1);
  addHostCounterToSnapshot("test_host_counter", 1, 1);
  addHostCounterToSnapshot("drop_host_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addGaugeToSnapshot("drop_gauge", 1);
  addHostGaugeToSnapshot("test_host_gauge", 4);
  addHostGaugeToSnapshot("drop_host_gauge", 4);
  addHistogramToSnapshot("test_histogram");
  addHistogramToSnapshot("drop_histogram");

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 5);

  EXPECT_NE(nullptr, findMetric(metrics, getTagExtractedName("test_counter")));
  EXPECT_EQ(nullptr, findMetric(metrics, getTagExtractedName("drop_counter")));
  EXPECT_NE(nullptr, findMetric(metrics, getTagExtractedName("test_host_counter")));
  EXPECT_EQ(nullptr, findMetric(metrics, getTagExtractedName("drop_host_counter")));
  EXPECT_NE(nullptr, findMetric(metrics, getTagExtractedName("test_gauge")));
  EXPECT_EQ(nullptr, findMetric(metrics, getTagExtractedName("drop_gauge")));
  EXPECT_NE(nullptr, findMetric(metrics, getTagExtractedName("test_host_gauge")));
  EXPECT_EQ(nullptr, findMetric(metrics, getTagExtractedName("drop_host_gauge")));
  EXPECT_NE(nullptr, findMetric(metrics, getTagExtractedName("test_histogram")));
  EXPECT_EQ(nullptr, findMetric(metrics, getTagExtractedName("drop_histogram")));
}

TEST_F(OtlpMetricsFlusherTests, OnNoMatchDrop) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "", {},
                                             R"pb(
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
                value_match {
                  safe_regex {
                    regex: ".*keep.*"
                  }
                }
              }
            }
            on_match {
              action {
                name: "otlp_metric_conversion_action"
                typed_config {
                  [type.googleapis.com/envoy.extensions.stat_sinks
                       .open_telemetry.v3.SinkConfig.ConversionAction] {
                         metric_name: "new_kept_metric"
                       }
                }
              }
            }
          }
        }
        on_no_match {
          action {
            name: "otlp_metric_drop_action"
            typed_config {
              [type.googleapis.com/envoy.extensions.stat_sinks
                  .open_telemetry.v3.SinkConfig.DropAction] {}
            }
          }
        }
      )pb"));

  addCounterToSnapshot("keep_counter", 1, 1);
  addCounterToSnapshot("drop_via_on_no_match_counter", 1, 1);

  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 1);

  EXPECT_NE(nullptr, findMetric(metrics, "new_kept_metric"));
  EXPECT_EQ(nullptr, findMetric(metrics, getTagExtractedName("drop_via_on_no_match_counter")));
}

TEST_F(OtlpMetricsFlusherTests, SetResourceAttributes) {
  OtlpMetricsFlusherImpl flusher(
      otlpOptions(true, false, true, true, "", {{"key_foo", "val_foo"}}));
  addCounterToSnapshot("test_counter1", 1, 1);
  MetricsExportRequestSharedPtr metrics =
      flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_);
  expectMetricsCount(metrics, 1);
  expectSum(metricAt(0, metrics), getTagExtractedName("test_counter1"), 1, true);
  EXPECT_EQ(1, metrics->resource_metrics().size());
  EXPECT_EQ(1, metrics->resource_metrics()[0].resource().attributes().size());
  EXPECT_EQ("key_foo", metrics->resource_metrics()[0].resource().attributes()[0].key());
  EXPECT_EQ("val_foo",
            metrics->resource_metrics()[0].resource().attributes()[0].value().string_value());
}

class MockOpenTelemetryGrpcMetricsExporter : public OpenTelemetryGrpcMetricsExporter {
public:
  MOCK_METHOD(void, send, (MetricsExportRequestPtr&&));
  MOCK_METHOD(void, onSuccess, (Grpc::ResponsePtr<MetricsExportResponse>&&, Tracing::Span&));
  MOCK_METHOD(void, onFailure, (Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&));
};

class MockOtlpMetricsFlusher : public OtlpMetricsFlusher {
public:
  MOCK_METHOD(MetricsExportRequestPtr, flush, (Stats::MetricSnapshot&, int64_t, int64_t),
              (const, override));
};

class OpenTelemetryGrpcSinkTests : public OpenTelemetryStatsSinkTests {
public:
  OpenTelemetryGrpcSinkTests()
      : flusher_(std::make_shared<MockOtlpMetricsFlusher>()),
        exporter_(std::make_shared<MockOpenTelemetryGrpcMetricsExporter>()) {}

  std::shared_ptr<MockOtlpMetricsFlusher> flusher_;
  std::shared_ptr<MockOpenTelemetryGrpcMetricsExporter> exporter_;
};

TEST_F(OpenTelemetryGrpcSinkTests, BasicFlow) {
  // Initialize the sink with a created_at time of 1000.
  OpenTelemetryGrpcSink sink(flusher_, exporter_, /*create_time_ns=*/1000);

  // First flush: last_flush_time_ns should be the created_at value (1000).
  MetricsExportRequestPtr request1 = std::make_unique<MetricsExportRequest>();
  EXPECT_CALL(*flusher_, flush(_, /*delta_start_time_ns=*/1000,
                               /*cumulative_start_time_ns=*/1000))
      .WillOnce(Return(ByMove(std::move(request1))));
  EXPECT_CALL(*exporter_, send(_));
  sink.flush(snapshot_);

  // Second flush: last_flush_time_ns should be the snapshotTime() of the
  // snapshot used in the first flush, which is expected_time_ns_.
  MetricsExportRequestPtr request2 = std::make_unique<MetricsExportRequest>();
  EXPECT_CALL(*flusher_, flush(_, /*delta_start_time_ns=*/expected_time_ns_,
                               /*cumulative_start_time_ns=*/1000))
      .WillOnce(Return(ByMove(std::move(request2))));
  EXPECT_CALL(*exporter_, send(_));
  sink.flush(snapshot_);
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
