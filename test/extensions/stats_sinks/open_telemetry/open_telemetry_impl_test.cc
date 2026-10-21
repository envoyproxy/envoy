#include <map>

#include "envoy/grpc/async_client.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

using testing::Contains;

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
      std::ignore = Protobuf::TextFormat::ParseFromString(
          metric_conversion_pbtext, sink_config.mutable_custom_metric_conversions());
    }
    return OtlpOptions::create(sink_config, resource, server_factory_context_).value();
  }

  int64_t getGaugeValueByName(const MetricsExportRequestSharedPtr& request,
                              const std::string& name) {
    for (const auto& metric : request->resource_metrics()[0].scope_metrics()[0].metrics()) {
      if (metric.name() == name) {
        if (!metric.has_gauge() || metric.gauge().data_points().empty()) {
          return -1;
        }
        return metric.gauge().data_points()[0].as_int();
      }
    }
    return -1;
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
                              const Stats::TagVector& tags = {{"hist_key", "hist_val"}},
                              bool add_values = true) {
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

    if (add_values) {
      for (auto value : values) {
        hist_insert(hist, value, 1);
      }
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
  auto response = Grpc::ResponsePtr<MetricsExportResponse>();
  response->mutable_partial_success()->set_rejected_data_points(1);
  exporter_->onSuccess(std::move(response), Tracing::NullSpan::instance());
}

class OtlpMetricsFlusherTests : public OpenTelemetryStatsSinkTests {
public:
  MetricsExportRequestSharedPtr flushToSingleRequest(const OtlpMetricsFlusherImpl& flusher) {
    std::vector<MetricsExportRequestPtr> requests;
    flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                  [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
    if (requests.size() != 1) {
      return nullptr;
    }
    return std::move(requests[0]);
  }

  void expectMetricsCount(const MetricsExportRequestSharedPtr& request, int count) {
    EXPECT_EQ(1, request->resource_metrics().size());
    EXPECT_EQ(1, request->resource_metrics()[0].scope_metrics().size());
    EXPECT_EQ(count, request->resource_metrics()[0].scope_metrics()[0].metrics().size());
  }

  const opentelemetry::proto::metrics::v1::Metric&
  metricAt(int index, const MetricsExportRequestSharedPtr& metrics) {
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
            if (attributes == nullptr) {
              return std::string("");
            }
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

  void sortDataPoints(opentelemetry::proto::metrics::v1::Metric& metric) {
    auto sort_by_attr = [](const auto& a, const auto& b) {
      auto get_attr_str = [](const auto& dp) {
        if (dp.attributes().empty()) {
          return std::string("");
        }
        std::vector<std::string> attrs;
        for (const auto& attr : dp.attributes()) {
          attrs.push_back(attr.key() + "=" + attr.value().string_value());
        }
        std::sort(attrs.begin(), attrs.end());
        std::string res;
        for (const auto& s : attrs) {
          res += s + ";";
        }
        return res;
      };
      return get_attr_str(a) < get_attr_str(b);
    };

    if (metric.has_gauge()) {
      std::sort(metric.mutable_gauge()->mutable_data_points()->begin(),
                metric.mutable_gauge()->mutable_data_points()->end(), sort_by_attr);
    } else if (metric.has_sum()) {
      std::sort(metric.mutable_sum()->mutable_data_points()->begin(),
                metric.mutable_sum()->mutable_data_points()->end(), sort_by_attr);
    } else if (metric.has_histogram()) {
      std::sort(metric.mutable_histogram()->mutable_data_points()->begin(),
                metric.mutable_histogram()->mutable_data_points()->end(), sort_by_attr);
    }
  }

  const opentelemetry::proto::metrics::v1::Metric*
  findMetric(const MetricsExportRequestSharedPtr& metrics, const std::string& name) {
    for (const auto& metric : metrics->resource_metrics()[0].scope_metrics()[0].metrics()) {
      if (metric.name() == name) {
        return &metric;
      }
    }
    return nullptr;
  }

  const opentelemetry::proto::metrics::v1::Metric*
  findGauge(const MetricsExportRequestSharedPtr& metrics, const std::string& name) {
    const auto* metric = findMetric(metrics, name);
    EXPECT_NE(metric, nullptr) << "Gauge metric '" << name << "' not found.";
    if (metric == nullptr) {
      return nullptr;
    }
    EXPECT_TRUE(metric->has_gauge());
    return metric;
  }

  const opentelemetry::proto::metrics::v1::Metric*
  findSum(const MetricsExportRequestSharedPtr& metrics, const std::string& name) {
    const auto* metric = findMetric(metrics, name);
    EXPECT_NE(metric, nullptr) << "Sum metric '" << name << "' not found.";
    if (metric == nullptr) {
      return nullptr;
    }
    EXPECT_TRUE(metric->has_sum());
    return metric;
  }

  const opentelemetry::proto::metrics::v1::Metric*
  findHistogram(const MetricsExportRequestSharedPtr& metrics, const std::string& name) {
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

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
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
  metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
  expectMetricsCount(metrics, /*count=*/4);
}

TEST_F(OtlpMetricsFlusherTests, MetricsWithStatsPrefix) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/false, /*report_histograms_as_deltas=*/false,
      /*emit_tags_as_attributes=*/true, /*use_tag_extracted_name=*/true, "prefix"));

  addCounterToSnapshot("test_counter", 1, 1);
  addHostCounterToSnapshot("test_host_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addGaugeToSnapshot("test_host_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
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
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/false, /*report_histograms_as_deltas=*/false,
      /*emit_tags_as_attributes=*/true, /*use_tag_extracted_name=*/false));

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
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
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/false, /*report_histograms_as_deltas=*/false,
      /*emit_tags_as_attributes=*/false, /*use_tag_extracted_name=*/true));

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");
  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
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

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
  expectMetricsCount(metrics, 4);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_gauge1")),
              getTagExtractedName("test_gauge1"), /*value=*/1);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_gauge2")),
              getTagExtractedName("test_gauge2"), /*value=*/2);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_host_gauge1")),
              getTagExtractedName("test_host_gauge1"), /*value=*/3);
  expectGauge(*findGauge(metrics, getTagExtractedName("test_host_gauge2")),
              getTagExtractedName("test_host_gauge2"), /*value=*/4);
}

TEST_F(OtlpMetricsFlusherTests, CumulativeCounterMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addCounterToSnapshot("test_counter1", 1, 1);
  addCounterToSnapshot("test_counter2", 2, 3);
  addHostCounterToSnapshot("test_host_counter1", 2, 4);
  addHostCounterToSnapshot("test_host_counter2", 5, 10);

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
  expectMetricsCount(metrics, 4);
  expectSum(*findSum(metrics, getTagExtractedName("test_counter1")),
            getTagExtractedName("test_counter1"), /*value=*/1, /*is_delta=*/false);
  expectSum(*findSum(metrics, getTagExtractedName("test_counter2")),
            getTagExtractedName("test_counter2"), /*value=*/3, /*is_delta=*/false);
  expectSum(*findSum(metrics, getTagExtractedName("test_host_counter1")),
            getTagExtractedName("test_host_counter1"), 4, false);
  expectSum(*findSum(metrics, getTagExtractedName("test_host_counter2")),
            getTagExtractedName("test_host_counter2"), 10, false);
}

TEST_F(OtlpMetricsFlusherTests, DeltaCounterMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, /*report_histograms_as_deltas=*/false,
      /*emit_tags_as_attributes=*/true, /*use_tag_extracted_name=*/true));

  addCounterToSnapshot("test_counter1", 1, 1);
  addCounterToSnapshot("test_counter2", 2, 3);
  addCounterToSnapshot("test_counter3", 0, 4);
  addHostCounterToSnapshot("test_host_counter1", 2, 4);
  addHostCounterToSnapshot("test_host_counter2", 5, 10);

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
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

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
  expectMetricsCount(metrics, 2);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram1")),
                  getTagExtractedName("test_histogram1"), /*is_delta=*/false);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram2")),
                  getTagExtractedName("test_histogram2"), /*is_delta=*/false);
}

TEST_F(OtlpMetricsFlusherTests, DeltaHistogramMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/false, /*report_histograms_as_deltas=*/true,
      /*emit_tags_as_attributes=*/true, /*use_tag_extracted_name=*/true));

  addHistogramToSnapshot("test_histogram1", true);
  addHistogramToSnapshot("test_histogram2", true);
  addHistogramToSnapshot("test_histogram3", true, true, {}, false);

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
  expectMetricsCount(metrics, 2);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram1")),
                  getTagExtractedName("test_histogram1"), /*is_delta=*/true);
  expectHistogram(*findHistogram(metrics, getTagExtractedName("test_histogram2")),
                  getTagExtractedName("test_histogram2"), /*is_delta=*/true);
}

TEST_F(OtlpMetricsFlusherTests, SetResourceAttributes) {
  OtlpMetricsFlusherImpl flusher(
      otlpOptions(true, false, true, true, "", {{"key_foo", "val_foo"}}));
  addCounterToSnapshot("test_counter1", 1, 1);

  auto metrics = flushToSingleRequest(flusher);
  ASSERT_NE(metrics, nullptr);
  expectMetricsCount(metrics, 1);
  expectSum(metricAt(0, metrics), getTagExtractedName("test_counter1"), 1, true);
  EXPECT_EQ(1, metrics->resource_metrics().size());
  EXPECT_EQ(1, metrics->resource_metrics()[0].resource().attributes().size());
  EXPECT_EQ("key_foo", metrics->resource_metrics()[0].resource().attributes()[0].key());
  EXPECT_EQ("val_foo",
            metrics->resource_metrics()[0].resource().attributes()[0].value().string_value());
}

TEST_F(OtlpMetricsFlusherTests, MaxDatapointsPerRequestNoLimits) {
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.set_max_data_points_per_request(0);
  Tracers::OpenTelemetry::Resource resource;
  auto options = OtlpOptions::create(sink_config, resource, server_factory_context_).value();
  OtlpMetricsFlusherImpl flusher(options);

  for (int i = 0; i < 10; ++i) {
    addCounterToSnapshot(fmt::format("counter{}", i), 1, 1);
  }

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });

  // 10 data points, 0 limit -> 1 request
  ASSERT_EQ(requests.size(), 1);
  EXPECT_EQ(10, requests[0]->resource_metrics(0).scope_metrics(0).metrics_size());
}

TEST_F(OtlpMetricsFlusherTests, MaxDatapointsPerRequestEmptyMetrics) {
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.set_max_data_points_per_request(10);
  Tracers::OpenTelemetry::Resource resource;
  auto options = OtlpOptions::create(sink_config, resource, server_factory_context_).value();
  OtlpMetricsFlusherImpl flusher(options);

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });

  EXPECT_EQ(requests.size(), 0);
}

TEST_F(OtlpMetricsFlusherTests, DeltaCountersAllZeroNoExport) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, /*report_histograms_as_deltas=*/false,
      /*emit_tags_as_attributes=*/true, /*use_tag_extracted_name=*/true));

  addCounterToSnapshot("test_counter1", 0, 1);
  addCounterToSnapshot("test_counter2", 0, 3);

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });

  EXPECT_EQ(requests.size(), 0);
}

TEST_F(OtlpMetricsFlusherTests, MaxDatapointsPerRequestWithLimits) {
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.set_max_data_points_per_request(2);
  Tracers::OpenTelemetry::Resource resource;
  auto options = OtlpOptions::create(sink_config, resource, server_factory_context_).value();
  OtlpMetricsFlusherImpl flusher(options);

  for (int i = 0; i < 5; ++i) {
    addCounterToSnapshot(fmt::format("counter{}", i), 1, 1);
  }

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });

  ASSERT_EQ(requests.size(), 3);
  EXPECT_EQ(2, requests[0]->resource_metrics(0).scope_metrics(0).metrics_size());
  EXPECT_EQ(2, requests[1]->resource_metrics(0).scope_metrics(0).metrics_size());
  EXPECT_EQ(1, requests[2]->resource_metrics(0).scope_metrics(0).metrics_size());
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

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
  ASSERT_EQ(1, requests.size());
  MetricsExportRequestSharedPtr metrics = std::move(requests[0]);
  const int expected_metrics_count = 2;
  expectMetricsCount(metrics, expected_metrics_count);

  auto& exported_metrics = *metrics->mutable_resource_metrics()
                                ->Mutable(0)
                                ->mutable_scope_metrics()
                                ->Mutable(0)
                                ->mutable_metrics();
  sortMetrics(exported_metrics);
  for (auto& m : exported_metrics) {
    sortDataPoints(m);
  }
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
  // data point 1: keyY: valY
  EXPECT_EQ(1, unmapped_metric.sum().data_points()[0].as_int());
  expectAttributes(unmapped_metric.sum().data_points()[0].attributes(), "keyX", "valX");
  // data point 2: keyX: valX
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

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
  ASSERT_EQ(1, requests.size());
  MetricsExportRequestSharedPtr metrics = std::move(requests[0]);
  const int expected_metrics_count = 2;
  expectMetricsCount(metrics, expected_metrics_count);

  auto& exported_metrics = *metrics->mutable_resource_metrics()
                                ->Mutable(0)
                                ->mutable_scope_metrics()
                                ->Mutable(0)
                                ->mutable_metrics();
  sortMetrics(exported_metrics);
  for (auto& m : exported_metrics) {
    sortDataPoints(m);
  }
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

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
  ASSERT_EQ(1, requests.size());
  MetricsExportRequestSharedPtr metrics = std::move(requests[0]);
  const int expected_metrics_count = 2;
  expectMetricsCount(metrics, expected_metrics_count);

  auto& exported_metrics = *metrics->mutable_resource_metrics()
                                ->Mutable(0)
                                ->mutable_scope_metrics()
                                ->Mutable(0)
                                ->mutable_metrics();
  sortMetrics(exported_metrics);
  for (auto& m : exported_metrics) {
    sortDataPoints(m);
  }
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
  EXPECT_NEAR(data_point1.sum(), 11656376.283404071, 0.1);
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

  addCounterToSnapshot("test_counter", /*delta=*/1, /*value=*/1, /*used=*/true, {{"key", "val1"}});
  addGaugeToSnapshot("test_gauge", /*value=*/1, /*used=*/true, {{"key", "valA"}});
  addHistogramToSnapshot("test_histogram", /*is_delta=*/false, /*used=*/true, {{"key", "hist1"}});

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
  ASSERT_EQ(1, requests.size());
  MetricsExportRequestSharedPtr metrics = std::move(requests[0]);
  expectMetricsCount(metrics, /*count=*/3);

  auto& exported_metrics = *metrics->mutable_resource_metrics()
                                ->Mutable(0)
                                ->mutable_scope_metrics()
                                ->Mutable(0)
                                ->mutable_metrics();
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
    std::vector<KeyValue> sorted_attrs(attrs.begin(), attrs.end());
    std::sort(sorted_attrs.begin(), sorted_attrs.end(),
              [](const KeyValue& a, const KeyValue& b) { return a.key() < b.key(); });
    EXPECT_EQ("key", sorted_attrs[0].key());
    EXPECT_EQ("val1", sorted_attrs[0].value().string_value());
    EXPECT_EQ("static_key_c", sorted_attrs[1].key());
    EXPECT_EQ("static_val_c", sorted_attrs[1].value().string_value());
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
    std::vector<KeyValue> sorted_attrs(attrs.begin(), attrs.end());
    std::sort(sorted_attrs.begin(), sorted_attrs.end(),
              [](const KeyValue& a, const KeyValue& b) { return a.key() < b.key(); });
    EXPECT_EQ("key", sorted_attrs[0].key());
    EXPECT_EQ("valA", sorted_attrs[0].value().string_value());
    EXPECT_EQ("static_key_g", sorted_attrs[1].key());
    EXPECT_EQ("static_val_g", sorted_attrs[1].value().string_value());
  }

  // Histogram: static_histogram
  {
    const auto& metric = exported_metrics[2];
    EXPECT_EQ("static_histogram", metric.name());
    EXPECT_TRUE(metric.has_histogram());
    EXPECT_EQ(1, metric.histogram().data_points().size());
    const auto& attrs = metric.histogram().data_points()[0].attributes();
    EXPECT_EQ(2, attrs.size());
    std::vector<KeyValue> sorted_attrs(attrs.begin(), attrs.end());
    std::sort(sorted_attrs.begin(), sorted_attrs.end(),
              [](const KeyValue& a, const KeyValue& b) { return a.key() < b.key(); });
    EXPECT_EQ("key", sorted_attrs[0].key());
    EXPECT_EQ("hist1", sorted_attrs[0].value().string_value());
    EXPECT_EQ("static_key_h", sorted_attrs[1].key());
    EXPECT_EQ("static_val_h", sorted_attrs[1].value().string_value());
  }
}

TEST_F(OtlpMetricsFlusherAggregationTests, MetricsWithDifferentlyOrderedTags) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "", {}, R"pb(
    matcher_list {
    }
  )pb"));

  // Add 4 counters with the same name but different tag orders (permutations of 3 labels)
  addCounterToSnapshot("test_counter", /*delta=*/1, /*value=*/1, /*used=*/true,
                       {{"a", "1"}, {"b", "2"}, {"c", "3"}});
  addCounterToSnapshot("test_counter", /*delta=*/2, /*value=*/2, /*used=*/true,
                       {{"b", "2"}, {"c", "3"}, {"a", "1"}});
  addCounterToSnapshot("test_counter", /*delta=*/3, /*value=*/3, /*used=*/true,
                       {{"c", "3"}, {"a", "1"}, {"b", "2"}});
  addCounterToSnapshot("test_counter", /*delta=*/4, /*value=*/4, /*used=*/true,
                       {{"a", "1"}, {"c", "3"}, {"b", "2"}});

  std::vector<MetricsExportRequestPtr> requests;

  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });

  ASSERT_EQ(1, requests.size());
  auto& exported_metrics = *requests[0]
                                ->mutable_resource_metrics()
                                ->Mutable(0)
                                ->mutable_scope_metrics()
                                ->Mutable(0)
                                ->mutable_metrics();

  // We expect exactly ONE counter in the output because they should be aggregated!
  ASSERT_EQ(1, exported_metrics.size());
  const auto& metric = exported_metrics[0];
  EXPECT_EQ(getTagExtractedName("test_counter"), metric.name());
  EXPECT_TRUE(metric.has_sum());

  // We expect only ONE data point because they should be aggregated!
  ASSERT_EQ(1, metric.sum().data_points().size());

  // Accumulated value should be aggregated (1 + 2 + 3 + 4 = 10)
  EXPECT_EQ(10, metric.sum().data_points()[0].as_int());

  // Tags should be sorted in the output as well!
  const auto& attrs = metric.sum().data_points()[0].attributes();
  EXPECT_EQ(3, attrs.size());
  EXPECT_EQ("a", attrs[0].key());
  EXPECT_EQ("1", attrs[0].value().string_value());
  EXPECT_EQ("b", attrs[1].key());
  EXPECT_EQ("2", attrs[1].value().string_value());
  EXPECT_EQ("c", attrs[2].key());
  EXPECT_EQ("3", attrs[2].value().string_value());
}

// Returns `attributes` as a map from key to string value.
std::map<std::string, std::string>
attributesMap(const Protobuf::RepeatedPtrField<KeyValue>& attributes) {
  std::map<std::string, std::string> result;
  for (const auto& attr : attributes) {
    result[attr.key()] = attr.value().string_value();
  }
  return result;
}

// Returns the ResourceMetrics of `request` whose resource sets `key` to `value`, or null.
const opentelemetry::proto::metrics::v1::ResourceMetrics*
findResourceMetrics(const MetricsExportRequest& request, absl::string_view key,
                    absl::string_view value) {
  for (const auto& resource_metrics : request.resource_metrics()) {
    for (const auto& attr : resource_metrics.resource().attributes()) {
      if (attr.key() == key && attr.value().string_value() == value) {
        return &resource_metrics;
      }
    }
  }
  return nullptr;
}

// Returns the attributes of the sink resource of a gateway container.
absl::flat_hash_map<std::string, std::string> proxyResource() {
  return {
      {"gcp.resource_type", "k8s_container"},
      {"location", "us-central1"},
      {"namespace_name", "proxy-ns"},
      {"pod_name", "proxy-pod"},
      {"container_name", "proxy"},
  };
}

// Returns a matcher of a matcher list, which converts the stats whose names start with
// `stat_prefix` by the ConversionAction that `action_pbtext` defines.
std::string conversionMatcher(absl::string_view stat_prefix, absl::string_view action_pbtext) {
  return absl::StrReplaceAll(R"pb(
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
          value_match { prefix: "{PREFIX}" }
        }
      }
      on_match {
        action {
          name: "otlp_metric_conversion"
          typed_config {
            [type.googleapis.com/
             envoy.extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction] {
              {ACTION}
            }
          }
        }
      }
    }
  )pb",
                             {{"{PREFIX}", stat_prefix}, {"{ACTION}", action_pbtext}});
}

// Returns custom metric conversions that consist of a matcher list with the given matchers.
std::string matcherList(const std::vector<std::string>& matchers) {
  return absl::StrCat("matcher_list {", absl::StrJoin(matchers, ""), "}");
}

// Returns a ConversionAction that converts a stat to `metric_name`, reported under the resource
// that `resource_conversion_pbtext` defines.
std::string conversionAction(absl::string_view metric_name,
                             absl::string_view resource_conversion_pbtext) {
  return absl::StrCat("metric_name: \"", metric_name, "\" resource_conversion {",
                      resource_conversion_pbtext, "}");
}

// The k8s_pod resource that the "namespace" and "pod" tags of a stat name.
constexpr absl::string_view PodResourceConversion = R"pb(
  static_resource_attributes {
    key: "gcp.resource_type"
    value { string_value: "k8s_pod" }
  }
  tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
  tags_to_resource_attributes { tag_name: "namespace" resource_attribute_key: "namespace_name" }
)pb";

// Converts the stats prefixed by "workload_counter" to "workload.network.io", reported under
// PodResourceConversion.
std::string podConversions() {
  return matcherList({conversionMatcher(
      "workload_counter", conversionAction("workload.network.io", PodResourceConversion))});
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionGroupsMetricsByResource) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(/*report_counters_as_deltas=*/true, false, true, true,
                                             "", proxyResource(), podConversions()));
  // Two stats of pod-a whose other tags are the same are aggregated into one data point.
  addCounterToSnapshot("workload_counter.1", /*delta=*/1, /*value=*/1, true,
                       {{"pod", "pod-a"}, {"namespace", "ns"}, {"direction", "sent"}});
  addCounterToSnapshot("workload_counter.2", /*delta=*/2, /*value=*/2, true,
                       {{"direction", "sent"}, {"namespace", "ns"}, {"pod", "pod-a"}});
  addCounterToSnapshot("workload_counter.3", /*delta=*/5, /*value=*/5, true,
                       {{"pod", "pod-b"}, {"namespace", "ns"}, {"direction", "sent"}});
  // A stat of the proxy itself stays on the sink resource.
  addCounterToSnapshot("proxy_counter", /*delta=*/7, /*value=*/7, true, {{"key", "val"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(3, request->resource_metrics_size());

  const auto* proxy = findResourceMetrics(*request, "pod_name", "proxy-pod");
  ASSERT_NE(proxy, nullptr);
  const auto proxy_resource = proxyResource();
  EXPECT_EQ(attributesMap(proxy->resource().attributes()),
            (std::map<std::string, std::string>(proxy_resource.begin(), proxy_resource.end())));
  ASSERT_EQ(1, proxy->scope_metrics_size());
  ASSERT_EQ(1, proxy->scope_metrics(0).metrics_size());
  EXPECT_EQ(getTagExtractedName("proxy_counter"), proxy->scope_metrics(0).metrics(0).name());

  for (const auto& [pod, value] :
       std::vector<std::pair<std::string, int>>{{"pod-a", 3}, {"pod-b", 5}}) {
    SCOPED_TRACE(pod);
    const auto* workload = findResourceMetrics(*request, "pod_name", pod);
    ASSERT_NE(workload, nullptr);
    // The resource has the static resource type and the attributes that the tags set, but none of
    // the attributes of the sink resource.
    EXPECT_EQ(attributesMap(workload->resource().attributes()),
              (std::map<std::string, std::string>{
                  {"gcp.resource_type", "k8s_pod"}, {"namespace_name", "ns"}, {"pod_name", pod}}));
    ASSERT_EQ(1, workload->scope_metrics_size());
    ASSERT_EQ(1, workload->scope_metrics(0).metrics_size());
    const auto& metric = workload->scope_metrics(0).metrics(0);
    EXPECT_EQ("workload.network.io", metric.name());
    ASSERT_EQ(1, metric.sum().data_points_size());
    EXPECT_EQ(value, metric.sum().data_points(0).as_int());
    // The tags that set resource attributes are not data point attributes.
    EXPECT_EQ(attributesMap(metric.sum().data_points(0).attributes()),
              (std::map<std::string, std::string>{{"direction", "sent"}}));
  }
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionGaugesAndHistograms) {
  constexpr absl::string_view PodNameConversion = R"pb(
    tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
  )pb";
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      false, /*report_histograms_as_deltas=*/false, true, true, "", proxyResource(),
      matcherList({conversionMatcher("workload_gauge",
                                     conversionAction("workload.gauge", PodNameConversion)),
                   conversionMatcher("workload_histogram",
                                     conversionAction("workload.histogram", PodNameConversion))})));
  addGaugeToSnapshot("workload_gauge", /*value=*/4, true, {{"pod", "pod-a"}});
  addHistogramToSnapshot("workload_histogram", /*is_delta=*/false, true,
                         {{"pod", "pod-a"}, {"role", "client"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(1, request->resource_metrics_size());
  auto& workload = *request->mutable_resource_metrics(0);
  EXPECT_EQ(attributesMap(workload.resource().attributes()),
            (std::map<std::string, std::string>{{"pod_name", "pod-a"}}));
  ASSERT_EQ(1, workload.scope_metrics_size());
  auto& metrics = *workload.mutable_scope_metrics(0)->mutable_metrics();
  sortMetrics(metrics);
  ASSERT_EQ(2, metrics.size());

  EXPECT_EQ("workload.gauge", metrics[0].name());
  ASSERT_EQ(1, metrics[0].gauge().data_points_size());
  EXPECT_EQ(4, metrics[0].gauge().data_points(0).as_int());
  EXPECT_TRUE(metrics[0].gauge().data_points(0).attributes().empty());

  EXPECT_EQ("workload.histogram", metrics[1].name());
  ASSERT_EQ(1, metrics[1].histogram().data_points_size());
  EXPECT_EQ(10, metrics[1].histogram().data_points(0).count());
  EXPECT_EQ(attributesMap(metrics[1].histogram().data_points(0).attributes()),
            (std::map<std::string, std::string>{{"role", "client"}}));
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionWithoutTags) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, false, true, true, "", proxyResource(),
      matcherList({conversionMatcher("node_counter", conversionAction("node.network.io", R"pb(
        static_resource_attributes {
          key: "gcp.resource_type"
          value { string_value: "k8s_node" }
        }
        static_resource_attributes {
          key: "node_name"
          value { string_value: "node-1" }
        }
      )pb"))})));
  addCounterToSnapshot("node_counter", /*delta=*/1, /*value=*/1, true, {{"direction", "sent"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(1, request->resource_metrics_size());
  EXPECT_EQ(attributesMap(request->resource_metrics(0).resource().attributes()),
            (std::map<std::string, std::string>{{"gcp.resource_type", "k8s_node"},
                                                {"node_name", "node-1"}}));
  EXPECT_EQ(attributesMap(request->resource_metrics(0)
                              .scope_metrics(0)
                              .metrics(0)
                              .sum()
                              .data_points(0)
                              .attributes()),
            (std::map<std::string, std::string>{{"direction", "sent"}}));
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionSkippedWithoutTagValues) {
  // The static metric label of the conversion applies whether the resource conversion does or not.
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, false, true, true, "", proxyResource(),
      matcherList({conversionMatcher(
          "workload_counter",
          absl::StrCat(conversionAction("workload.network.io", PodResourceConversion), R"pb(
            static_metric_labels {
              key: "source"
              value { string_value: "envoy" }
            }
          )pb"))})));
  addCounterToSnapshot("workload_counter.converted", /*delta=*/1, /*value=*/1, true,
                       {{"pod", "pod-a"}, {"namespace", "ns"}});
  // The stats below lack a tag of the conversion, or have it with an empty value.
  addCounterToSnapshot("workload_counter.no_namespace", /*delta=*/2, /*value=*/2, true,
                       {{"pod", "pod-a"}});
  addCounterToSnapshot("workload_counter.empty_namespace", /*delta=*/4, /*value=*/4, true,
                       {{"pod", "pod-a"}, {"namespace", ""}});
  addCounterToSnapshot("workload_counter.no_tags", /*delta=*/8, /*value=*/8, true,
                       {{"direction", "sent"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(2, request->resource_metrics_size());

  const auto* workload = findResourceMetrics(*request, "pod_name", "pod-a");
  ASSERT_NE(workload, nullptr);
  ASSERT_EQ(1, workload->scope_metrics(0).metrics_size());
  const auto& converted = workload->scope_metrics(0).metrics(0);
  EXPECT_EQ("workload.network.io", converted.name());
  ASSERT_EQ(1, converted.sum().data_points_size());
  EXPECT_EQ(1, converted.sum().data_points(0).as_int());
  EXPECT_EQ(attributesMap(converted.sum().data_points(0).attributes()),
            (std::map<std::string, std::string>{{"source", "envoy"}}));

  // The other stats are reported under the sink resource with all of their tags, as if they had
  // no resource conversion, but with the metric name and the static label of the conversion.
  const auto* proxy = findResourceMetrics(*request, "pod_name", "proxy-pod");
  ASSERT_NE(proxy, nullptr);
  ASSERT_EQ(1, proxy->scope_metrics(0).metrics_size());
  const auto& skipped = proxy->scope_metrics(0).metrics(0);
  EXPECT_EQ("workload.network.io", skipped.name());
  std::map<std::map<std::string, std::string>, int64_t> points;
  for (const auto& point : skipped.sum().data_points()) {
    points[attributesMap(point.attributes())] = point.as_int();
  }
  EXPECT_EQ(points, (std::map<std::map<std::string, std::string>, int64_t>{
                        {{{"pod", "pod-a"}, {"source", "envoy"}}, 2},
                        {{{"namespace", ""}, {"pod", "pod-a"}, {"source", "envoy"}}, 4},
                        {{{"direction", "sent"}, {"source", "envoy"}}, 8}}));
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionSharedAcrossActions) {
  // "a_counter" and "c_counter" are both converted to "m1", and "b_counter" to "m2", each by an
  // action with the same resource conversion.
  constexpr absl::string_view PodNameConversion = R"pb(
    tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
  )pb";
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, false, true, true, "", proxyResource(),
      matcherList({conversionMatcher("a_counter", conversionAction("m1", PodNameConversion)),
                   conversionMatcher("b_counter", conversionAction("m2", PodNameConversion)),
                   conversionMatcher("c_counter", conversionAction("m1", PodNameConversion))})));
  addCounterToSnapshot("a_counter", /*delta=*/1, /*value=*/1, true, {{"pod", "pod-a"}});
  addCounterToSnapshot("b_counter", /*delta=*/2, /*value=*/2, true, {{"pod", "pod-a"}});
  addCounterToSnapshot("c_counter", /*delta=*/4, /*value=*/4, true, {{"pod", "pod-a"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  // The resources of the three actions have the same attributes, so they are one resource.
  ASSERT_EQ(1, request->resource_metrics_size());
  auto& metrics =
      *request->mutable_resource_metrics(0)->mutable_scope_metrics(0)->mutable_metrics();
  sortMetrics(metrics);
  ASSERT_EQ(2, metrics.size());
  EXPECT_EQ("m1", metrics[0].name());
  // "a_counter" and "c_counter" are aggregated, since they have the same resource, name and
  // attributes.
  ASSERT_EQ(1, metrics[0].sum().data_points_size());
  EXPECT_EQ(5, metrics[0].sum().data_points(0).as_int());
  EXPECT_EQ("m2", metrics[1].name());
  ASSERT_EQ(1, metrics[1].sum().data_points_size());
  EXPECT_EQ(2, metrics[1].sum().data_points(0).as_int());
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionToSinkResource) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, false, true, true, "",
      {{"location", "us-central1"}, {"pod_name", "proxy-pod"}},
      matcherList(
          {conversionMatcher("workload_counter", conversionAction("workload.network.io", R"pb(
        static_resource_attributes {
          key: "location"
          value { string_value: "us-central1" }
        }
        tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
      )pb"))})));
  // The resource of this stat has the same attributes as the sink resource.
  addCounterToSnapshot("workload_counter", /*delta=*/1, /*value=*/1, true, {{"pod", "proxy-pod"}});
  addCounterToSnapshot("proxy_counter", /*delta=*/2, /*value=*/2, true, {{"key", "val"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  // So both metrics are reported under the same ResourceMetrics.
  ASSERT_EQ(1, request->resource_metrics_size());
  EXPECT_EQ(
      attributesMap(request->resource_metrics(0).resource().attributes()),
      (std::map<std::string, std::string>{{"location", "us-central1"}, {"pod_name", "proxy-pod"}}));
  EXPECT_EQ(2, request->resource_metrics(0).scope_metrics(0).metrics_size());
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionTagToSeveralAttributes) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, false, true, true, "", {},
      matcherList(
          {conversionMatcher("workload_counter", conversionAction("workload.network.io", R"pb(
        tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
        tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "k8s.pod.name" }
        tags_to_resource_attributes { tag_name: "zone" }
      )pb"))})));
  addCounterToSnapshot("workload_counter", /*delta=*/1, /*value=*/1, true,
                       {{"pod", "pod-a"}, {"zone", "us-central1-a"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(1, request->resource_metrics_size());
  // The resource attribute key of "zone" is the tag name.
  EXPECT_EQ(attributesMap(request->resource_metrics(0).resource().attributes()),
            (std::map<std::string, std::string>{
                {"k8s.pod.name", "pod-a"}, {"pod_name", "pod-a"}, {"zone", "us-central1-a"}}));
  EXPECT_TRUE(request->resource_metrics(0)
                  .scope_metrics(0)
                  .metrics(0)
                  .sum()
                  .data_points(0)
                  .attributes()
                  .empty());
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionWithoutEmittingTags) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(/*report_counters_as_deltas=*/true, false,
                                             /*emit_tags_as_attributes=*/false, true, "",
                                             proxyResource(), podConversions()));
  addCounterToSnapshot("workload_counter.1", /*delta=*/1, /*value=*/1, true,
                       {{"pod", "pod-a"}, {"namespace", "ns"}, {"direction", "sent"}});
  // Lacks the "namespace" tag, so it is reported under the sink resource.
  addCounterToSnapshot("workload_counter.2", /*delta=*/2, /*value=*/2, true,
                       {{"pod", "pod-a"}, {"direction", "sent"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(2, request->resource_metrics_size());
  // The tags still set the resource attributes, but no tag is a data point attribute.
  for (const auto& [pod, value] :
       std::vector<std::pair<std::string, int>>{{"pod-a", 1}, {"proxy-pod", 2}}) {
    SCOPED_TRACE(pod);
    const auto* resource_metrics = findResourceMetrics(*request, "pod_name", pod);
    ASSERT_NE(resource_metrics, nullptr);
    const auto& point = resource_metrics->scope_metrics(0).metrics(0).sum().data_points(0);
    EXPECT_EQ(value, point.as_int());
    EXPECT_TRUE(point.attributes().empty());
  }
}

TEST_F(OtlpMetricsFlusherAggregationTests, ResourceConversionWithMaxDataPointsPerRequest) {
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.set_report_counters_as_deltas(true);
  sink_config.set_max_data_points_per_request(2);
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(
      podConversions(), sink_config.mutable_custom_metric_conversions()));
  Tracers::OpenTelemetry::Resource resource;
  resource.attributes_ = proxyResource();
  OtlpMetricsFlusherImpl flusher(
      OtlpOptions::create(sink_config, resource, server_factory_context_).value());
  for (int i = 0; i < 3; ++i) {
    addCounterToSnapshot(absl::StrCat("workload_counter.", i), /*delta=*/1, /*value=*/1, true,
                         {{"pod", absl::StrCat("pod-", i)}, {"namespace", "ns"}});
    addCounterToSnapshot(absl::StrCat("workload_counter.other.", i), /*delta=*/1, /*value=*/1, true,
                         {{"pod", absl::StrCat("pod-", i)}, {"namespace", "ns"}, {"other", "x"}});
  }
  addCounterToSnapshot("proxy_counter", /*delta=*/1, /*value=*/1);

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });

  // 7 data points, at most 2 per request.
  ASSERT_EQ(4, requests.size());
  absl::flat_hash_map<std::string, int> points_by_pod;
  for (const auto& request : requests) {
    int points = 0;
    absl::flat_hash_set<std::string> pods_in_request;
    for (const auto& resource_metrics : request->resource_metrics()) {
      const std::string pod = attributesMap(resource_metrics.resource().attributes())["pod_name"];
      // Each resource has one ResourceMetrics per request.
      EXPECT_TRUE(pods_in_request.insert(pod).second) << pod;
      for (const auto& metric : resource_metrics.scope_metrics(0).metrics()) {
        points += metric.sum().data_points_size();
        points_by_pod[pod] += metric.sum().data_points_size();
      }
    }
    EXPECT_LE(points, 2);
  }
  EXPECT_EQ(points_by_pod, (absl::flat_hash_map<std::string, int>{
                               {"pod-0", 2}, {"pod-1", 2}, {"pod-2", 2}, {"proxy-pod", 1}}));
}

TEST_F(OtlpMetricsFlusherAggregationTests,
       ResourceConversionGaugesAndHistogramsWithMaxDataPointsPerRequest) {
  constexpr absl::string_view PodNameConversion = R"pb(
    tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
  )pb";
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.set_max_data_points_per_request(1);
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(
      matcherList({conversionMatcher("workload_gauge",
                                     conversionAction("workload.gauge", PodNameConversion)),
                   conversionMatcher("workload_histogram",
                                     conversionAction("workload.histogram", PodNameConversion))}),
      sink_config.mutable_custom_metric_conversions()));
  OtlpMetricsFlusherImpl flusher(
      OtlpOptions::create(sink_config, Tracers::OpenTelemetry::Resource(), server_factory_context_)
          .value());
  for (const std::string& pod : std::vector<std::string>{"pod-a", "pod-b"}) {
    addGaugeToSnapshot(absl::StrCat("workload_gauge.", pod), /*value=*/1, true, {{"pod", pod}});
    addHistogramToSnapshot(absl::StrCat("workload_histogram.", pod), /*is_delta=*/false, true,
                           {{"pod", pod}});
  }

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });

  // One data point per request, under the resource of its pod.
  ASSERT_EQ(4, requests.size());
  std::map<std::pair<std::string, std::string>, int> points;
  for (const auto& request : requests) {
    ASSERT_EQ(1, request->resource_metrics_size());
    const auto& resource_metrics = request->resource_metrics(0);
    ASSERT_EQ(1, resource_metrics.scope_metrics(0).metrics_size());
    const auto& metric = resource_metrics.scope_metrics(0).metrics(0);
    points[{attributesMap(resource_metrics.resource().attributes())["pod_name"], metric.name()}] +=
        metric.gauge().data_points_size() + metric.histogram().data_points_size();
  }
  EXPECT_EQ(points, (std::map<std::pair<std::string, std::string>, int>{
                        {{"pod-a", "workload.gauge"}, 1},
                        {{"pod-a", "workload.histogram"}, 1},
                        {{"pod-b", "workload.gauge"}, 1},
                        {{"pod-b", "workload.histogram"}, 1}}));
}

TEST_F(OtlpMetricsFlusherAggregationTests, ConversionWithoutMetricNameKeepsStatNames) {
  // The action has no metric name, so it doesn't merge the stats that it matches into one metric.
  OtlpMetricsFlusherImpl flusher(otlpOptions(
      /*report_counters_as_deltas=*/true, false, true, true, "prefix", {},
      matcherList({conversionMatcher("workload_counter", R"pb(
        resource_conversion {
          tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
        }
      )pb")})));
  addCounterToSnapshot("workload_counter.sent", /*delta=*/1, /*value=*/1, true, {{"pod", "pod-a"}});
  addCounterToSnapshot("workload_counter.received", /*delta=*/2, /*value=*/2, true,
                       {{"pod", "pod-a"}});

  auto request = flushToSingleRequest(flusher);
  ASSERT_NE(request, nullptr);
  ASSERT_EQ(1, request->resource_metrics_size());
  auto& metrics =
      *request->mutable_resource_metrics(0)->mutable_scope_metrics(0)->mutable_metrics();
  sortMetrics(metrics);
  ASSERT_EQ(2, metrics.size());
  EXPECT_EQ(absl::StrCat("prefix.", getTagExtractedName("workload_counter.received")),
            metrics[0].name());
  EXPECT_EQ(2, metrics[0].sum().data_points(0).as_int());
  EXPECT_EQ(absl::StrCat("prefix.", getTagExtractedName("workload_counter.sent")),
            metrics[1].name());
  EXPECT_EQ(1, metrics[1].sum().data_points(0).as_int());
}

class ResourceConversionConfigTests : public OpenTelemetryStatsSinkTests {
public:
  // Returns the status of creating the options of a sink with a conversion whose resource
  // conversion `resource_conversion_pbtext` defines.
  absl::Status createStatus(absl::string_view resource_conversion_pbtext) {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(
        matcherList({conversionMatcher(
            "workload", conversionAction("workload.network.io", resource_conversion_pbtext))}),
        sink_config.mutable_custom_metric_conversions()));
    Tracers::OpenTelemetry::Resource resource;
    resource.attributes_ = proxyResource();
    return OtlpOptions::create(sink_config, resource, server_factory_context_).status();
  }
};

TEST_F(ResourceConversionConfigTests, ValidConfig) {
  EXPECT_TRUE(createStatus(PodResourceConversion).ok());
}

TEST_F(ResourceConversionConfigTests, RejectsResourceAttributeSetMoreThanOnce) {
  for (const auto& [resource_conversion, key] : std::vector<std::pair<std::string, std::string>>{
           // Static and set by a tag.
           {R"pb(
              static_resource_attributes {
                key: "pod_name"
                value { string_value: "pod-a" }
              }
              tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
            )pb",
            "pod_name"},
           // Static and set by a tag, whose resource attribute key is its name.
           {R"pb(
              static_resource_attributes {
                key: "pod"
                value { string_value: "pod-a" }
              }
              tags_to_resource_attributes { tag_name: "pod" }
            )pb",
            "pod"},
           // Static twice.
           {R"pb(
              static_resource_attributes {
                key: "gcp.resource_type"
                value { string_value: "k8s_pod" }
              }
              static_resource_attributes {
                key: "gcp.resource_type"
                value { string_value: "k8s_container" }
              }
            )pb",
            "gcp.resource_type"},
           // Set by two tags.
           {R"pb(
              tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "name" }
              tags_to_resource_attributes { tag_name: "service" resource_attribute_key: "name" }
            )pb",
            "name"},
           // Set by the same tag twice.
           {R"pb(
              tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
              tags_to_resource_attributes { tag_name: "pod" resource_attribute_key: "pod_name" }
            )pb",
            "pod_name"},
       }) {
    SCOPED_TRACE(resource_conversion);
    const absl::Status status = createStatus(resource_conversion);
    EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code());
    EXPECT_EQ(
        absl::StrCat("resource_conversion: resource attribute '", key, "' is set more than once"),
        status.message());
  }
}

TEST_F(ResourceConversionConfigTests, RejectsInvalidStaticAttributes) {
  {
    const absl::Status status = createStatus(R"pb(
      static_resource_attributes {
        key: ""
        value { string_value: "k8s_pod" }
      }
    )pb");
    EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code());
    EXPECT_EQ("resource_conversion: the keys of static_resource_attributes must not be empty",
              status.message());
  }
  for (const absl::string_view value : {"value { int_value: 3 }", ""}) {
    SCOPED_TRACE(value);
    const absl::Status status =
        createStatus(absl::StrCat("static_resource_attributes { key: \"replicas\" ", value, " }"));
    EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code());
    EXPECT_EQ("resource_conversion: the value of static resource attribute 'replicas' must be a "
              "string",
              status.message());
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

  addCounterToSnapshot("test_counter", /*delta=*/1, /*value=*/1);
  addCounterToSnapshot("drop_counter", /*delta=*/1, /*value=*/1);
  addHostCounterToSnapshot("test_host_counter", /*delta=*/1, /*value=*/1);
  addHostCounterToSnapshot("drop_host_counter", /*delta=*/1, /*value=*/1);
  addGaugeToSnapshot("test_gauge", /*value=*/1);
  addGaugeToSnapshot("drop_gauge", /*value=*/1);
  addHostGaugeToSnapshot("test_host_gauge", /*value=*/4);
  addHostGaugeToSnapshot("drop_host_gauge", /*value=*/4);
  addHistogramToSnapshot("test_histogram");
  addHistogramToSnapshot("drop_histogram");

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
  ASSERT_EQ(1, requests.size());
  MetricsExportRequestSharedPtr metrics = std::move(requests[0]);
  expectMetricsCount(metrics, /*count=*/5);

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

  addCounterToSnapshot("keep_counter", /*delta=*/1, /*value=*/1);
  addCounterToSnapshot("drop_via_on_no_match_counter", /*delta=*/1, /*value=*/1);

  std::vector<MetricsExportRequestPtr> requests;
  flusher.flush(snapshot_, delta_start_time_ns_, cumulative_start_time_ns_,
                [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
  ASSERT_EQ(1, requests.size());
  MetricsExportRequestSharedPtr metrics = std::move(requests[0]);
  expectMetricsCount(metrics, /*count=*/1);

  EXPECT_NE(nullptr, findMetric(metrics, "new_kept_metric"));
  EXPECT_EQ(nullptr, findMetric(metrics, getTagExtractedName("drop_via_on_no_match_counter")));
}

class MockOpenTelemetryGrpcMetricsExporter : public OpenTelemetryGrpcMetricsExporter {
public:
  MOCK_METHOD(void, send, (MetricsExportRequestPtr&&));
  MOCK_METHOD(void, onSuccess, (Grpc::ResponsePtr<MetricsExportResponse>&&, Tracing::Span&));
  MOCK_METHOD(void, onFailure, (Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&));
};

class MockOtlpMetricsFlusher : public OtlpMetricsFlusher {
public:
  MOCK_METHOD(void, flush,
              (Stats::MetricSnapshot&, int64_t, int64_t,
               absl::AnyInvocable<void(MetricsExportRequestPtr)>),
              (const, override));
};

class OpenTelemetrySinkTests : public OpenTelemetryStatsSinkTests {
public:
  OpenTelemetrySinkTests()
      : flusher_(std::make_shared<MockOtlpMetricsFlusher>()),
        exporter_(std::make_shared<MockOpenTelemetryGrpcMetricsExporter>()) {}

  std::shared_ptr<MockOtlpMetricsFlusher> flusher_;
  std::shared_ptr<MockOpenTelemetryGrpcMetricsExporter> exporter_;
};

TEST_F(OpenTelemetrySinkTests, BasicFlow) {
  // Initialize the sink with a created_at time of 1000.
  OpenTelemetrySink sink(flusher_, exporter_, /*create_time_ns=*/1000);

  // First flush: last_flush_time_ns should be the created_at value (1000).
  MetricsExportRequestPtr request1 = std::make_unique<MetricsExportRequest>();
  EXPECT_CALL(*flusher_, flush(_, /*delta_start_time_ns=*/1000,
                               /*cumulative_start_time_ns=*/1000, _))
      .WillOnce(Invoke([&](Stats::MetricSnapshot&, int64_t, int64_t,
                           absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) {
        send_callback(std::move(request1));
      }));
  EXPECT_CALL(*exporter_, send(_));
  sink.flush(snapshot_);

  // Second flush: last_flush_time_ns should be the snapshotTime() of the
  // snapshot used in the first flush, which is expected_time_ns_.
  MetricsExportRequestPtr request2 = std::make_unique<MetricsExportRequest>();
  EXPECT_CALL(*flusher_, flush(_, /*delta_start_time_ns=*/expected_time_ns_,
                               /*cumulative_start_time_ns=*/1000, _))
      .WillOnce(Invoke([&](Stats::MetricSnapshot&, int64_t, int64_t,
                           absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) {
        send_callback(std::move(request2));
      }));
  EXPECT_CALL(*exporter_, send(_));
  sink.flush(snapshot_);
}

class MetricAggregatorTests : public testing::Test {
public:
  using AggregationTemporality = opentelemetry::proto::metrics::v1::AggregationTemporality;

  void setupAggregator(AggregationTemporality counter_temp, AggregationTemporality hist_temp) {
    metric_aggregator_ = std::make_unique<MetricAggregator>(counter_temp, hist_temp);
  }

  // Returns the aggregation result of the sink resource, which is expected to be the only
  // resource that data points were added under.
  MetricAggregator::AggregationResult releaseSinkResourceResult() {
    std::vector<MetricAggregator::AggregationResult> results = metric_aggregator_->releaseResults();
    EXPECT_LE(results.size(), 1);
    if (results.empty()) {
      return {};
    }
    return std::move(results[ResourceRegistry::SinkResourceIndex]);
  }

  class CustomMockHistogramStatistics : public Stats::HistogramStatistics {
  public:
    std::string quantileSummary() const override { return ""; }
    std::string bucketSummary() const override { return ""; }
    const std::vector<double>& supportedQuantiles() const override { return supported_quantiles_; }
    const std::vector<double>& computedQuantiles() const override { return computed_quantiles_; }
    Stats::ConstSupportedBuckets& supportedBuckets() const override { return supported_buckets_; }
    const std::vector<uint64_t>& computedBuckets() const override { return computed_buckets_; }
    std::vector<uint64_t> computeDisjointBuckets() const override { return computed_buckets_; }
    uint64_t sampleCount() const override { return sample_count_; }
    uint64_t outOfBoundCount() const override { return 0; }
    double sampleSum() const override { return sample_sum_; }

    std::vector<double> supported_quantiles_;
    std::vector<double> computed_quantiles_;
    std::vector<double> supported_buckets_;
    std::vector<uint64_t> computed_buckets_;
    uint64_t sample_count_{0};
    double sample_sum_{0};
  };

  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attributes_;
  std::unique_ptr<MetricAggregator> metric_aggregator_;
  CustomMockHistogramStatistics custom_stats_;

  void mockHistogram(const std::vector<double>& supported_buckets,
                     const std::vector<uint64_t>& computed_buckets, uint64_t sample_count,
                     uint64_t sample_sum) {
    custom_stats_.supported_buckets_ = supported_buckets;
    custom_stats_.computed_buckets_ = computed_buckets;
    custom_stats_.sample_count_ = sample_count;
    custom_stats_.sample_sum_ = sample_sum;
  }

  uint64_t getGaugeValue(const MetricAggregator::AggregationResult& metrics,
                         const std::string& name,
                         const MetricAggregator::SortedAttributesVector& attrs) {
    MetricAggregator::MetricKey key{std::string(name),
                                    MetricAggregator::SortedAttributesVector(attrs)};
    auto it = metrics.gauge_data_.find(key);
    if (it != metrics.gauge_data_.end()) {
      return it->second;
    }
    return 0;
  }

  uint64_t getCounterValue(const MetricAggregator::AggregationResult& metrics,
                           const std::string& name,
                           const MetricAggregator::SortedAttributesVector& attrs) {
    MetricAggregator::MetricKey key{std::string(name),
                                    MetricAggregator::SortedAttributesVector(attrs)};
    auto it = metrics.counter_data_.find(key);
    if (it != metrics.counter_data_.end()) {
      return it->second;
    }
    return 0;
  }

  const MetricAggregator::CustomHistogram*
  getHistogramPoint(const MetricAggregator::AggregationResult& metrics, const std::string& name,
                    const MetricAggregator::SortedAttributesVector& attrs) {
    MetricAggregator::MetricKey key{std::string(name),
                                    MetricAggregator::SortedAttributesVector(attrs)};
    auto it = metrics.histogram_data_.find(key);
    if (it != metrics.histogram_data_.end()) {
      return &it->second;
    }
    return nullptr;
  }
};

TEST_F(MetricAggregatorTests, GaugeAggregation) {
  setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                  /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);
  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  MetricAggregator::SortedAttributesVector attr2 = {{"key2", ""}};

  metric_aggregator_->addGauge("metric1", /*value=*/10,
                               MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addGauge("metric2", /*value=*/20,
                               MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addGauge(
      "metric1", /*value=*/30,
      MetricAggregator::SortedAttributesVector{{"key1", ""}}); // Aggregated with 10 (10+30=40)
  metric_aggregator_->addGauge("metric3", /*value=*/40,
                               MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addGauge(
      "metric1", /*value=*/50,
      MetricAggregator::SortedAttributesVector{{"key2", ""}}); // New point for `attr2`
  metric_aggregator_->addGauge("metric4", /*value=*/60,
                               MetricAggregator::SortedAttributesVector{{"key1", ""}});

  auto metrics = releaseSinkResourceResult();
  EXPECT_EQ(metrics.gauge_data_.size(), 5);
  EXPECT_EQ(getGaugeValue(metrics, "metric1", attr1), 40); // Aggregated
  EXPECT_EQ(getGaugeValue(metrics, "metric2", attr1), 20);
  EXPECT_EQ(getGaugeValue(metrics, "metric1", attr2), 50);
  EXPECT_EQ(getGaugeValue(metrics, "metric3", attr1), 40);
  EXPECT_EQ(getGaugeValue(metrics, "metric4", attr1), 60);
}

TEST_F(MetricAggregatorTests, CounterAggregationDelta) {
  setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA,
                  /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);
  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  MetricAggregator::SortedAttributesVector attr2 = {{"key2", ""}};

  metric_aggregator_->addCounter("metric1", /*value=*/5,
                                 MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addCounter("metric2", /*value=*/15,
                                 MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addCounter(
      "metric1", /*value=*/10,
      MetricAggregator::SortedAttributesVector{{"key2", ""}}); // Diff attribute
  metric_aggregator_->addCounter("metric1", /*value=*/20,
                                 MetricAggregator::SortedAttributesVector{{"key1", ""}});

  auto metrics = releaseSinkResourceResult();
  EXPECT_EQ(metrics.counter_data_.size(), 3);
  EXPECT_EQ(getCounterValue(metrics, "metric1", attr1), 25); // 5 + 20 (delta)
  EXPECT_EQ(getCounterValue(metrics, "metric2", attr1), 15);
  EXPECT_EQ(getCounterValue(metrics, "metric1", attr2), 10);
}
TEST_F(MetricAggregatorTests, CounterAggregationCumulative) {
  setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                  /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);
  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  MetricAggregator::SortedAttributesVector attr2 = {{"key2", ""}};

  metric_aggregator_->addCounter("metric1", /*value=*/10,
                                 MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addCounter("metric2", /*value=*/20,
                                 MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addCounter(
      "metric1", /*value=*/15,
      MetricAggregator::SortedAttributesVector{{"key2", ""}}); // Diff attribute
  metric_aggregator_->addCounter(
      "metric1", /*value=*/25,
      MetricAggregator::SortedAttributesVector{{"key1", ""}}); // Collides with first

  auto metrics = releaseSinkResourceResult();
  EXPECT_EQ(metrics.counter_data_.size(), 3);
  EXPECT_EQ(getCounterValue(metrics, "metric1", attr1), 35); // 10 + 25 (value)
  EXPECT_EQ(getCounterValue(metrics, "metric2", attr1), 20);
  EXPECT_EQ(getCounterValue(metrics, "metric1", attr2), 15);
}

TEST_F(MetricAggregatorTests, CounterAggregationDifferentTagOrders) {
  setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                  /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);

  // They must be sorted when creating SortedAttributesVector!
  MetricAggregator::SortedAttributesVector attr1 = {{"a", "1"}, {"b", "2"}, {"c", "3"}};
  MetricAggregator::SortedAttributesVector attr2 = {
      {"a", "1"}, {"b", "2"}, {"c", "3"}}; // Identical!

  metric_aggregator_->addCounter(
      "metric_perm", /*value=*/1,
      MetricAggregator::SortedAttributesVector{{"a", "1"}, {"b", "2"}, {"c", "3"}});
  metric_aggregator_->addCounter(
      "metric_perm", /*value=*/2,
      MetricAggregator::SortedAttributesVector{{"a", "1"}, {"b", "2"}, {"c", "3"}});

  auto metrics = releaseSinkResourceResult();
  EXPECT_EQ(metrics.counter_data_.size(), 1);
  EXPECT_EQ(getCounterValue(metrics, "metric_perm", attr1), 3); // 1 + 2 = 3
}

TEST_F(MetricAggregatorTests, HistogramAggregationCumulative) {
  setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                  /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);
  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  MetricAggregator::SortedAttributesVector attr2 = {{"key2", ""}};

  std::vector<double> supported_buckets = {10, 20, 30};
  std::vector<uint64_t> computed_buckets1 = {1, 2, 3};
  std::vector<uint64_t> computed_buckets2 = {4, 5, 6};

  mockHistogram(supported_buckets, computed_buckets1, 6, 60);
  metric_aggregator_->addHistogram("metric1", custom_stats_,
                                   MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addHistogram("metric2", custom_stats_,
                                   MetricAggregator::SortedAttributesVector{{"key1", ""}});
  metric_aggregator_->addHistogram(
      "metric1", custom_stats_,
      MetricAggregator::SortedAttributesVector{{"key2", ""}}); // Diff attribute

  mockHistogram(supported_buckets, computed_buckets2, 15, 150);
  metric_aggregator_->addHistogram(
      "metric1", custom_stats_,
      MetricAggregator::SortedAttributesVector{{"key1", ""}}); // Aggregates with first

  auto metrics = releaseSinkResourceResult();
  EXPECT_EQ(metrics.histogram_data_.size(), 3);

  auto* dp1 = getHistogramPoint(metrics, "metric1", attr1);
  ASSERT_NE(dp1, nullptr);
  EXPECT_EQ(dp1->count_, 21); // 6 + 15
  EXPECT_EQ(dp1->sum_, 210);

  auto* dp2 = getHistogramPoint(metrics, "metric2", attr1);
  ASSERT_NE(dp2, nullptr);
  EXPECT_EQ(dp2->count_, 6);

  auto* dp3 =
      getHistogramPoint(metrics, "metric1", MetricAggregator::SortedAttributesVector{{"key2", ""}});
  ASSERT_NE(dp3, nullptr);
  EXPECT_EQ(dp3->count_, 6);
}

TEST_F(MetricAggregatorTests, HistogramAggregationDelta) {
  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};

  std::vector<double> supported_buckets = {10, 20, 30};

  {
    setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                    /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA);

    // 1. Add a 0-count histogram -> should be ignored (early return)
    metric_aggregator_->addHistogram("metric1", custom_stats_,
                                     MetricAggregator::SortedAttributesVector{{"key1", ""}});

    auto metrics = releaseSinkResourceResult();
    EXPECT_TRUE(metrics.histogram_data_.empty());
  }

  {
    setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                    /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA);

    // 2. Add valid data
    std::vector<uint64_t> computed_buckets1 = {1, 2, 3};
    mockHistogram(supported_buckets, computed_buckets1, 6, 60);
    metric_aggregator_->addHistogram("metric1", custom_stats_,
                                     MetricAggregator::SortedAttributesVector{{"key1", ""}});
    metric_aggregator_->addHistogram("metric2", custom_stats_,
                                     MetricAggregator::SortedAttributesVector{{"key1", ""}});
    metric_aggregator_->addHistogram(
        "metric1", custom_stats_,
        MetricAggregator::SortedAttributesVector{{"key2", ""}}); // Diff attribute

    // 3. Add colliding data -> should aggregate (sum up)
    std::vector<uint64_t> computed_buckets2 = {4, 5, 6};
    mockHistogram(supported_buckets, computed_buckets2, 15, 150);
    metric_aggregator_->addHistogram("metric1", custom_stats_,
                                     MetricAggregator::SortedAttributesVector{{"key1", ""}});

    auto metrics = releaseSinkResourceResult();
    EXPECT_EQ(metrics.histogram_data_.size(), 3);

    auto* dp1 = getHistogramPoint(metrics, "metric1", attr1);
    ASSERT_NE(dp1, nullptr);
    EXPECT_EQ(dp1->count_, 21); // 6 + 15
    EXPECT_EQ(dp1->sum_, 210);

    auto* dp2 = getHistogramPoint(metrics, "metric2", attr1);
    ASSERT_NE(dp2, nullptr);
    EXPECT_EQ(dp2->count_, 6);

    auto* dp3 = getHistogramPoint(metrics, "metric1",
                                  MetricAggregator::SortedAttributesVector{{"key2", ""}});
    ASSERT_NE(dp3, nullptr);
    EXPECT_EQ(dp3->count_, 6);
  }
}

TEST_F(MetricAggregatorTests, HistogramAggregationBoundariesMismatch) {
  setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                  /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  std::vector<double> supported_buckets1 = {10, 20, 30};
  std::vector<uint64_t> computed_buckets1 = {1, 2, 3};

  mockHistogram(supported_buckets1, computed_buckets1, 6, 60);
  metric_aggregator_->addHistogram("metric1", custom_stats_,
                                   MetricAggregator::SortedAttributesVector{{"key1", ""}});

  // Add histogram with different boundaries
  std::vector<double> supported_buckets2 = {10, 25, 30}; // Diff boundary
  std::vector<uint64_t> computed_buckets2 = {4, 5, 6};
  mockHistogram(supported_buckets2, computed_buckets2, 15, 150);
  EXPECT_LOG_CONTAINS("error", "Histogram bounds mismatch", {
    metric_aggregator_->addHistogram("metric1", custom_stats_,
                                     MetricAggregator::SortedAttributesVector{{"key1", ""}});
  });

  auto metrics = releaseSinkResourceResult();
  EXPECT_EQ(metrics.histogram_data_.size(),
            1); // Should not aggregate, so only 1 entry (the first one)

  auto it = metrics.histogram_data_.begin();
  EXPECT_EQ(it->first.name(), "metric1");
  EXPECT_EQ(it->second.count_, 6); // Only first histogram data
  EXPECT_EQ(it->second.sum_, 60);
}

class RequestStreamerTests : public testing::Test {
public:
  void SetUp() override { resource_attributes_.Add()->set_key("resource_key"); }

  void setupStreamer(uint32_t max_data_point, bool enable_metric_aggregation = true) {
    opentelemetry::proto::metrics::v1::AggregationTemporality counter_temp = opentelemetry::proto::
        metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE;
    opentelemetry::proto::metrics::v1::AggregationTemporality hist_temp = opentelemetry::proto::
        metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE;

    resources_ = std::make_unique<ResourceRegistry>(resource_attributes_);
    streamer_ = std::make_unique<RequestStreamer>(
        max_data_point, *resources_, counter_temp, hist_temp,
        [this](MetricsExportRequestPtr request) { requests_.push_back(std::move(request)); },
        /*snapshot_time_ns=*/0, /*delta_start_time_ns=*/0, /*cumulative_start_time_ns=*/0,
        enable_metric_aggregation);
  }

  // Returns the index of the resource of the given pod.
  uint32_t podResource(absl::string_view pod) {
    return resources_->findOrAdd(pod_conversion_, {pod});
  }

  // Returns the value of the "pod" attribute of the given resource.
  static std::string podOf(const opentelemetry::proto::resource::v1::Resource& resource) {
    for (const auto& attribute : resource.attributes()) {
      if (attribute.key() == "pod") {
        return attribute.value().string_value();
      }
    }
    return "";
  }

  // A resource conversion whose resource only has the "pod" attribute, set by the "pod" tag.
  static ConversionAction::ResourceConversion podConversion() {
    ConversionAction::ResourceConversion conversion;
    conversion.tags.push_back({"pod", {"pod"}});
    conversion.tag_indexes.emplace("pod", 0);
    return conversion;
  }

  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> resource_attributes_;
  const ConversionAction::ResourceConversion pod_conversion_ = podConversion();
  std::vector<MetricsExportRequestPtr> requests_;
  std::unique_ptr<ResourceRegistry> resources_;
  std::unique_ptr<RequestStreamer> streamer_;
};

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestEmptyMetrics) {
  setupStreamer(/*max_data_point=*/1);
  streamer_->send();
  EXPECT_EQ(requests_.size(), 0);
}

TEST_F(RequestStreamerTests, TestNoAggregationEmitsSeparateMetrics) {
  setupStreamer(/*max_data_point=*/0, /*enable_metric_aggregation=*/false);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", "val1"}};
  MetricAggregator::SortedAttributesVector attr2 = {{"key2", "val2"}};

  streamer_->addGauge("metric1", 10, std::move(attr1));
  streamer_->addGauge("metric1", 20, std::move(attr2));
  streamer_->send();

  EXPECT_EQ(requests_.size(), 1);
  // We expect 2 separate Metric protos with the same name because aggregation is disabled!
  EXPECT_EQ(requests_[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);

  EXPECT_EQ(requests_[0]->resource_metrics(0).scope_metrics(0).metrics(0).name(), "metric1");
  EXPECT_EQ(requests_[0]->resource_metrics(0).scope_metrics(0).metrics(1).name(), "metric1");
}

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestNoLimits) {
  setupStreamer(/*max_data_point=*/0);

  for (int i = 0; i < 1000; i++) {
    streamer_->addGauge("metric" + std::to_string(i), i, {});
  }
  streamer_->send();

  EXPECT_EQ(requests_.size(), 1);
  EXPECT_EQ(requests_[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1000);
}

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestWithLimits) {
  setupStreamer(/*max_data_point=*/2);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  streamer_->addGauge("metric1", 1, {});
  streamer_->addGauge("metric1", 2, std::move(attr1));
  streamer_->addGauge("metric2", 3, {});
  streamer_->send();

  EXPECT_EQ(requests_.size(), 2);

  auto count_dp = [](const MetricsExportRequestPtr& req) {
    uint32_t count = 0;
    for (const auto& metric : req->resource_metrics(0).scope_metrics(0).metrics()) {
      count += metric.gauge().data_points_size();
    }
    return count;
  };

  EXPECT_EQ(count_dp(requests_[0]), 2);
  EXPECT_EQ(count_dp(requests_[1]), 1);
}

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestGauge) {
  setupStreamer(/*max_data_point=*/1);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  MetricAggregator::SortedAttributesVector attr2 = {{"key2", ""}};

  streamer_->addGauge("metric1", 1, std::move(attr1));
  streamer_->addGauge("metric1", 2, std::move(attr2));
  streamer_->send();

  EXPECT_EQ(requests_.size(), 2);
  EXPECT_EQ(requests_[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(requests_[1]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
}

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestAggregationCounter) {
  setupStreamer(/*max_data_point=*/2);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};
  MetricAggregator::SortedAttributesVector attr2 = {{"key2", ""}};

  streamer_->addCounter("metric1", 10, {});
  streamer_->addCounter("metric1", 20, std::move(attr1));
  streamer_->addCounter("metric1", 30, std::move(attr2));
  streamer_->send();
  EXPECT_EQ(requests_.size(), 2);

  auto count_dp = [](const MetricsExportRequestPtr& req) {
    uint32_t count = 0;
    for (const auto& metric : req->resource_metrics(0).scope_metrics(0).metrics()) {
      count += metric.sum().data_points_size(); // Counters are Sum metrics in OTLP
    }
    return count;
  };

  EXPECT_EQ(count_dp(requests_[0]), 2);
  EXPECT_EQ(count_dp(requests_[1]), 1);

  // Verify each point's value is what we expect (10, 20, 30), order is non-deterministic.
  std::set<uint64_t> expected_values = {10, 20, 30};
  for (const auto& req : requests_) {
    for (const auto& metric : req->resource_metrics(0).scope_metrics(0).metrics()) {
      for (const auto& dp : metric.sum().data_points()) {
        EXPECT_THAT(expected_values, Contains(dp.as_int()));
        expected_values.erase(dp.as_int());
      }
    }
  }
  EXPECT_TRUE(expected_values.empty());
}

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestNoAggregationCounter) {
  setupStreamer(/*max_data_point=*/1);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};

  streamer_->addCounter("metric1", 10, {});
  streamer_->addCounter("metric1", 20, std::move(attr1));
  streamer_->send();

  EXPECT_EQ(requests_.size(), 2);
  EXPECT_EQ(requests_[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(
      requests_[0]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points(0).as_int(),
      10);
  EXPECT_EQ(requests_[1]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(
      requests_[1]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points(0).as_int(),
      20);
}

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestAggregationHistogram) {
  setupStreamer(/*max_data_point=*/2);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};

  MetricAggregator::CustomHistogram hist1{1, 10.0, {1}, {10.0}};
  MetricAggregator::CustomHistogram hist2{2, 20.0, {2}, {20.0}};
  MetricAggregator::CustomHistogram hist3{3, 30.0, {3}, {30.0}};

  streamer_->addHistogram("metric1", hist1, {});
  streamer_->addHistogram("metric1", hist2, std::move(attr1));
  streamer_->addHistogram("metric2", hist3, {});
  streamer_->send();
  EXPECT_EQ(requests_.size(), 2);

  auto count_dp = [](const MetricsExportRequestPtr& req) {
    uint32_t count = 0;
    for (const auto& metric : req->resource_metrics(0).scope_metrics(0).metrics()) {
      count += metric.histogram().data_points_size(); // Histograms use .histogram()
    }
    return count;
  };

  EXPECT_EQ(count_dp(requests_[0]), 2);
  EXPECT_EQ(count_dp(requests_[1]), 1);

  // Verify each point's count and sum is what we expect, order is non-deterministic.
  std::set<uint64_t> expected_counts = {1, 2, 3};
  for (const auto& req : requests_) {
    for (const auto& metric : req->resource_metrics(0).scope_metrics(0).metrics()) {
      for (const auto& dp : metric.histogram().data_points()) {
        EXPECT_THAT(expected_counts, Contains(dp.count()));
        expected_counts.erase(dp.count());
        if (dp.count() == 1) {
          EXPECT_EQ(dp.sum(), 10.0);
        } else if (dp.count() == 2) {
          EXPECT_EQ(dp.sum(), 20.0);
        } else if (dp.count() == 3) {
          EXPECT_EQ(dp.sum(), 30.0);
        }
      }
    }
  }
  EXPECT_TRUE(expected_counts.empty());
}

TEST_F(RequestStreamerTests, TestMaxDatapointsPerRequestNoAggregationHistogram) {
  setupStreamer(/*max_data_point=*/1);

  MetricAggregator::SortedAttributesVector attr1 = {{"key1", ""}};

  MetricAggregator::CustomHistogram hist1{1, 10.0, {1}, {10.0}};
  MetricAggregator::CustomHistogram hist2{2, 20.0, {2}, {20.0}};

  streamer_->addHistogram("metric1", hist1, {});
  streamer_->addHistogram("metric1", hist2, std::move(attr1));
  streamer_->send();

  EXPECT_EQ(requests_.size(), 2);

  EXPECT_EQ(requests_.size(), 2);
  EXPECT_EQ(requests_[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(requests_[1]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
}

TEST_F(RequestStreamerTests, TestResourcesWithoutAggregation) {
  setupStreamer(/*max_data_point=*/0, /*enable_metric_aggregation=*/false);
  const uint32_t pod_a = podResource("a");

  streamer_->addGauge("metric1", 1, {}, pod_a);
  streamer_->addGauge("metric1", 2, {});
  streamer_->addGauge("metric2", 3, {}, pod_a);
  streamer_->send();

  // Each resource has one ResourceMetrics, added along with its first data point.
  ASSERT_EQ(requests_.size(), 1);
  ASSERT_EQ(requests_[0]->resource_metrics_size(), 2);
  const auto& pod_a_metrics = requests_[0]->resource_metrics(0);
  ASSERT_EQ(pod_a_metrics.resource().attributes_size(), 1);
  EXPECT_EQ(pod_a_metrics.resource().attributes(0).key(), "pod");
  EXPECT_EQ(pod_a_metrics.resource().attributes(0).value().string_value(), "a");
  ASSERT_EQ(pod_a_metrics.scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(pod_a_metrics.scope_metrics(0).metrics(0).gauge().data_points(0).as_int(), 1);
  EXPECT_EQ(pod_a_metrics.scope_metrics(0).metrics(1).gauge().data_points(0).as_int(), 3);

  const auto& sink_metrics = requests_[0]->resource_metrics(1);
  ASSERT_EQ(sink_metrics.resource().attributes_size(), 1);
  EXPECT_EQ(sink_metrics.resource().attributes(0).key(), "resource_key");
  ASSERT_EQ(sink_metrics.scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(sink_metrics.scope_metrics(0).metrics(0).gauge().data_points(0).as_int(), 2);
}

TEST_F(RequestStreamerTests, TestResourcesWithAggregation) {
  setupStreamer(/*max_data_point=*/0);
  const uint32_t pod_a = podResource("a");

  streamer_->addCounter("metric", 1, {});
  streamer_->addCounter("metric", 2, {}, pod_a);
  streamer_->addCounter("metric", 3, MetricAggregator::SortedAttributesVector{{"key", "value"}},
                        pod_a);
  streamer_->send();

  // The data points of a metric under different resources are reported in different metrics.
  ASSERT_EQ(requests_.size(), 1);
  ASSERT_EQ(requests_[0]->resource_metrics_size(), 2);
  const auto& sink_metrics = requests_[0]->resource_metrics(0);
  ASSERT_EQ(sink_metrics.scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(sink_metrics.scope_metrics(0).metrics(0).sum().data_points_size(), 1);
  const auto& pod_a_metrics = requests_[0]->resource_metrics(1);
  ASSERT_EQ(pod_a_metrics.scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(pod_a_metrics.scope_metrics(0).metrics(0).sum().data_points_size(), 2);
}

TEST_F(RequestStreamerTests, TestResourcesHaveOneResourceMetricsPerRequest) {
  setupStreamer(/*max_data_point=*/3);
  const uint32_t pod_a = podResource("a");

  streamer_->addGauge("metric1", 1, {}, pod_a);
  streamer_->addGauge("metric1", 2, {});
  // The same resource as the first point, in the same request.
  streamer_->addGauge("metric1", 3, MetricAggregator::SortedAttributesVector{{"key", "val"}},
                      pod_a);
  // The request is full, so the point goes to a new request.
  streamer_->addGauge("metric1", 4, {}, pod_a);
  streamer_->send();

  ASSERT_EQ(requests_.size(), 2);
  ASSERT_EQ(requests_[0]->resource_metrics_size(), 2);
  const auto& pod_a_metrics = requests_[0]->resource_metrics(0);
  EXPECT_EQ(podOf(pod_a_metrics.resource()), "a");
  // Both points of the resource are under the same metric.
  ASSERT_EQ(pod_a_metrics.scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(pod_a_metrics.scope_metrics(0).metrics(0).gauge().data_points_size(), 2);
  const auto& sink_metrics = requests_[0]->resource_metrics(1);
  ASSERT_EQ(sink_metrics.resource().attributes_size(), 1);
  EXPECT_EQ(sink_metrics.resource().attributes(0).key(), "resource_key");
  EXPECT_EQ(sink_metrics.scope_metrics(0).metrics(0).gauge().data_points(0).as_int(), 2);

  ASSERT_EQ(requests_[1]->resource_metrics_size(), 1);
  EXPECT_EQ(podOf(requests_[1]->resource_metrics(0).resource()), "a");
  EXPECT_EQ(
      requests_[1]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points(0).as_int(),
      4);
}

TEST_F(RequestStreamerTests, TestResourceWithoutAttributes) {
  resource_attributes_.Clear();
  setupStreamer(/*max_data_point=*/0);

  streamer_->addGauge("metric", 1, {});
  streamer_->send();

  ASSERT_EQ(requests_.size(), 1);
  ASSERT_EQ(requests_[0]->resource_metrics_size(), 1);
  EXPECT_FALSE(requests_[0]->resource_metrics(0).has_resource());
}

TEST_F(RequestStreamerTests, TestAddAggregationResultsByResource) {
  setupStreamer(/*max_data_point=*/2);
  const uint32_t pod_a = podResource("a");
  const uint32_t pod_b = podResource("b");

  MetricAggregator aggregator(AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                              AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);
  for (int i = 0; i < 3; ++i) {
    for (const uint32_t pod : {pod_a, pod_b}) {
      aggregator.addCounter(
          "metric", 1, MetricAggregator::SortedAttributesVector{{"key", absl::StrCat(i)}}, pod);
    }
  }
  streamer_->addAggregationResults(aggregator.releaseResults());
  streamer_->send();

  // The data points are added one resource after another.
  ASSERT_EQ(requests_.size(), 3);
  ASSERT_EQ(requests_[0]->resource_metrics_size(), 1);
  EXPECT_EQ(podOf(requests_[0]->resource_metrics(0).resource()), "a");
  ASSERT_EQ(requests_[1]->resource_metrics_size(), 2);
  EXPECT_EQ(podOf(requests_[1]->resource_metrics(0).resource()), "a");
  EXPECT_EQ(podOf(requests_[1]->resource_metrics(1).resource()), "b");
  ASSERT_EQ(requests_[2]->resource_metrics_size(), 1);
  EXPECT_EQ(podOf(requests_[2]->resource_metrics(0).resource()), "b");
}

TEST_F(MetricAggregatorTests, AggregationByResource) {
  setupAggregator(/*counter_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                  /*hist_temp=*/AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);
  MetricAggregator::SortedAttributesVector attr = {{"key", "value"}};

  metric_aggregator_->addCounter("metric", /*value=*/1,
                                 MetricAggregator::SortedAttributesVector{{"key", "value"}});
  metric_aggregator_->addCounter("metric", /*value=*/2,
                                 MetricAggregator::SortedAttributesVector{{"key", "value"}},
                                 /*resource_index=*/2);
  metric_aggregator_->addGauge("metric", /*value=*/3,
                               MetricAggregator::SortedAttributesVector{{"key", "value"}},
                               /*resource_index=*/2);
  mockHistogram({10}, {1}, 1, 5);
  metric_aggregator_->addHistogram("metric", custom_stats_,
                                   MetricAggregator::SortedAttributesVector{{"key", "value"}},
                                   /*resource_index=*/2);
  metric_aggregator_->addCounter("metric", /*value=*/4,
                                 MetricAggregator::SortedAttributesVector{{"key", "value"}},
                                 /*resource_index=*/2);

  // The data points of each resource are aggregated separately.
  std::vector<MetricAggregator::AggregationResult> results = metric_aggregator_->releaseResults();
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0].counter_data_.size(), 1);
  EXPECT_EQ(getCounterValue(results[0], "metric", attr), 1);
  EXPECT_TRUE(results[0].gauge_data_.empty());
  EXPECT_TRUE(results[0].histogram_data_.empty());
  EXPECT_TRUE(results[1].counter_data_.empty());
  EXPECT_TRUE(results[1].gauge_data_.empty());
  EXPECT_TRUE(results[1].histogram_data_.empty());
  EXPECT_EQ(getCounterValue(results[2], "metric", attr), 6);
  EXPECT_EQ(getGaugeValue(results[2], "metric", attr), 3);
  ASSERT_NE(getHistogramPoint(results[2], "metric", attr), nullptr);
  EXPECT_EQ(getHistogramPoint(results[2], "metric", attr)->count_, 1);
}

class ResourceRegistryTests : public testing::Test {
public:
  using Attributes = std::vector<std::pair<std::string, std::string>>;

  ResourceRegistryTests() {
    addSinkAttribute("pod_name", "envoy");
    addSinkAttribute("location", "us-central1");
  }

  void addSinkAttribute(const std::string& key, const std::string& value) {
    auto* attribute = sink_attributes_.Add();
    attribute->set_key(key);
    attribute->mutable_value()->set_string_value(value);
  }

  // Returns a resource conversion with the given static attributes, whose tags set the resource
  // attributes with the given keys.
  static ConversionAction::ResourceConversion
  resourceConversion(Attributes static_attributes,
                     const std::vector<std::pair<std::string, std::vector<std::string>>>& tags) {
    ConversionAction::ResourceConversion conversion;
    conversion.static_attributes = std::move(static_attributes);
    for (const auto& [name, attribute_keys] : tags) {
      conversion.tag_indexes.emplace(name, static_cast<uint32_t>(conversion.tags.size()));
      conversion.tags.push_back({name, absl::InlinedVector<std::string, 1>(attribute_keys.begin(),
                                                                           attribute_keys.end())});
    }
    return conversion;
  }

  // Returns the attributes of `resource`, in order.
  static Attributes attributesOf(const opentelemetry::proto::resource::v1::Resource& resource) {
    Attributes attributes;
    for (const auto& attribute : resource.attributes()) {
      attributes.emplace_back(attribute.key(), attribute.value().string_value());
    }
    return attributes;
  }

  Protobuf::RepeatedPtrField<KeyValue> sink_attributes_;
};

TEST_F(ResourceRegistryTests, SinkResource) {
  ResourceRegistry resources(sink_attributes_);

  // The sink resource keeps the order of its attributes.
  EXPECT_EQ(attributesOf(resources.get(ResourceRegistry::SinkResourceIndex)),
            (Attributes{{"pod_name", "envoy"}, {"location", "us-central1"}}));
}

TEST_F(ResourceRegistryTests, FindOrAdd) {
  const ConversionAction::ResourceConversion conversion =
      resourceConversion({{"gcp.resource_type", "k8s_pod"}},
                         {{"pod", {"pod_name"}}, {"namespace", {"namespace_name"}}});
  ResourceRegistry resources(sink_attributes_);

  const uint32_t pod_a = resources.findOrAdd(conversion, {"a", "ns"});
  const uint32_t pod_b = resources.findOrAdd(conversion, {"b", "ns"});
  EXPECT_EQ(pod_a, 1);
  EXPECT_EQ(pod_b, 2);
  EXPECT_EQ(resources.findOrAdd(conversion, {"a", "ns"}), pod_a);

  // A resource has the static attributes of the conversion and the attributes that its tags set,
  // sorted by key.
  EXPECT_EQ(
      attributesOf(resources.get(pod_a)),
      (Attributes{{"gcp.resource_type", "k8s_pod"}, {"namespace_name", "ns"}, {"pod_name", "a"}}));
  EXPECT_EQ(
      attributesOf(resources.get(pod_b)),
      (Attributes{{"gcp.resource_type", "k8s_pod"}, {"namespace_name", "ns"}, {"pod_name", "b"}}));
}

TEST_F(ResourceRegistryTests, TagToSeveralAttributes) {
  const ConversionAction::ResourceConversion conversion =
      resourceConversion({}, {{"pod", {"pod_name", "k8s.pod.name"}}});
  ResourceRegistry resources(sink_attributes_);

  EXPECT_EQ(attributesOf(resources.get(resources.findOrAdd(conversion, {"a"}))),
            (Attributes{{"k8s.pod.name", "a"}, {"pod_name", "a"}}));
}

TEST_F(ResourceRegistryTests, ResourcesWithTheSameAttributes) {
  const ConversionAction::ResourceConversion pod_conversion =
      resourceConversion({}, {{"pod", {"pod_name"}}});
  const ConversionAction::ResourceConversion name_conversion =
      resourceConversion({}, {{"name", {"pod_name"}}});
  const ConversionAction::ResourceConversion located_pod_conversion =
      resourceConversion({{"location", "us-central1"}}, {{"pod", {"pod_name"}}});
  ResourceRegistry resources(sink_attributes_);

  // Resources that different conversions resolve to are the same resource if they have the same
  // attributes.
  const uint32_t pod_a = resources.findOrAdd(pod_conversion, {"a"});
  EXPECT_EQ(resources.findOrAdd(name_conversion, {"a"}), pod_a);
  EXPECT_NE(resources.findOrAdd(located_pod_conversion, {"a"}), pod_a);
  // Including the sink resource.
  EXPECT_EQ(resources.findOrAdd(located_pod_conversion, {"envoy"}),
            ResourceRegistry::SinkResourceIndex);
}

TEST_F(ResourceRegistryTests, OwnsTagValues) {
  const ConversionAction::ResourceConversion conversion =
      resourceConversion({}, {{"pod", {"pod_name"}}});
  ResourceRegistry resources(sink_attributes_);

  uint32_t index;
  {
    std::string pod = "a";
    index = resources.findOrAdd(conversion, {pod});
    pod = "overwritten";
  }
  const std::string pod = "a";
  EXPECT_EQ(resources.findOrAdd(conversion, {pod}), index);
  EXPECT_EQ(attributesOf(resources.get(index)), (Attributes{{"pod_name", "a"}}));
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
