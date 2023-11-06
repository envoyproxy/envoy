#include "envoy/grpc/async_client.h"

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"

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

  const OtlpOptionsSharedPtr otlpOptions(bool report_counters_as_deltas = false,
                                         bool report_histograms_as_deltas = false,
                                         bool emit_tags_as_attributes = true,
                                         bool use_tag_extracted_name = true,
                                         const std::string& stat_prefix = "") {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.set_report_counters_as_deltas(report_counters_as_deltas);
    sink_config.set_report_histograms_as_deltas(report_histograms_as_deltas);
    sink_config.mutable_emit_tags_as_attributes()->set_value(emit_tags_as_attributes);
    sink_config.mutable_use_tag_extracted_name()->set_value(use_tag_extracted_name);
    sink_config.set_prefix(stat_prefix);

    return std::make_shared<OtlpOptions>(sink_config);
  }

  std::string getTagExtractedName(const std::string name) { return name + "-tagged"; }

  void addCounterToSnapshot(const std::string& name, uint64_t delta, uint64_t value,
                            bool used = true) {
    counter_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockCounter>>());
    counter_storage_.back()->name_ = name;
    counter_storage_.back()->setTagExtractedName(getTagExtractedName(name));
    counter_storage_.back()->value_ = value;
    counter_storage_.back()->used_ = used;
    counter_storage_.back()->setTags({{"counter_key", "counter_val"}});

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

  void addGaugeToSnapshot(const std::string& name, uint64_t value, bool used = true) {
    gauge_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockGauge>>());
    gauge_storage_.back()->name_ = name;
    gauge_storage_.back()->setTagExtractedName(getTagExtractedName(name));
    gauge_storage_.back()->value_ = value;
    gauge_storage_.back()->used_ = used;
    gauge_storage_.back()->setTags({{"gauge_key", "gauge_val"}});

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

  void addHistogramToSnapshot(const std::string& name, bool is_delta = false, bool used = true) {
    auto histogram = std::make_unique<NiceMock<Stats::MockParentHistogram>>();

    histogram_t* hist = hist_alloc();
    for (uint64_t value = 1; value <= 10; value++) {
      hist_insert_intscale(hist, is_delta ? value + 10 : value, 0, 1);
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
    histogram_storage_.back()->setTags({{"hist_key", "hist_val"}});

    snapshot_.histograms_.push_back(*histogram_storage_.back());
  }

  long long int expected_time_ns_;
  std::vector<histogram_t*> histogram_ptrs_;
  std::vector<std::unique_ptr<Stats::HistogramStatisticsImpl>> hist_stats_;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockCounter>>> counter_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockGauge>>> gauge_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockParentHistogram>>> histogram_storage_;
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

  void expectGauge(const opentelemetry::proto::metrics::v1::Metric& metric, std::string name,
                   int value) {
    EXPECT_EQ(name, metric.name());
    EXPECT_TRUE(metric.has_gauge());
    EXPECT_EQ(1, metric.gauge().data_points().size());
    EXPECT_EQ(value, metric.gauge().data_points()[0].as_int());
    EXPECT_EQ(expected_time_ns_, metric.gauge().data_points()[0].time_unix_nano());
  }

  void expectSum(const opentelemetry::proto::metrics::v1::Metric& metric, std::string name,
                 int value, bool is_delta) {
    EXPECT_EQ(name, metric.name());
    EXPECT_TRUE(metric.has_sum());
    EXPECT_TRUE(metric.sum().is_monotonic());

    if (is_delta) {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA,
                metric.sum().aggregation_temporality());
    } else {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                metric.sum().aggregation_temporality());
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
    EXPECT_EQ(default_buckets_count, data_point.bucket_counts().size());
    EXPECT_EQ(default_buckets_count, data_point.explicit_bounds().size());
    EXPECT_EQ(expected_time_ns_, data_point.time_unix_nano());

    if (is_delta) {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA,
                metric.histogram().aggregation_temporality());

      EXPECT_GE(data_point.sum(), 160);
    } else {
      EXPECT_EQ(AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
                metric.histogram().aggregation_temporality());

      EXPECT_GE(data_point.sum(), 55);
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
};

TEST_F(OtlpMetricsFlusherTests, MetricsWithDefaultOptions) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addCounterToSnapshot("test_counter", 1, 1);
  addHostCounterToSnapshot("test_host_counter", 2, 3);
  addGaugeToSnapshot("test_gauge", 1);
  addHostGaugeToSnapshot("test_host_gauge", 4);
  addHistogramToSnapshot("test_histogram");

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 5);

  {
    const auto metric = metricAt(0, metrics);
    expectGauge(metric, getTagExtractedName("test_gauge"), 1);
    expectAttributes(metric.gauge().data_points()[0].attributes(), "gauge_key", "gauge_val");
  }

  {
    const auto metric = metricAt(1, metrics);
    expectGauge(metric, getTagExtractedName("test_host_gauge"), 4);
    expectAttributes(metric.gauge().data_points()[0].attributes(), "gauge_key", "gauge_val");
  }

  {
    const auto metric = metricAt(2, metrics);
    expectSum(metric, getTagExtractedName("test_counter"), 1, false);
    expectAttributes(metric.sum().data_points()[0].attributes(), "counter_key", "counter_val");
  }

  {
    const auto metric = metricAt(3, metrics);
    expectSum(metric, getTagExtractedName("test_host_counter"), 3, false);
    expectAttributes(metric.sum().data_points()[0].attributes(), "counter_key", "counter_val");
  }

  {
    const auto metric = metricAt(4, metrics);
    expectHistogram(metric, getTagExtractedName("test_histogram"), false);
    expectAttributes(metric.histogram().data_points()[0].attributes(), "hist_key", "hist_val");
  }

  gauge_storage_.back()->used_ = false;
  metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 4);
}

TEST_F(OtlpMetricsFlusherTests, MetricsWithStatsPrefix) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, true, "prefix"));

  addCounterToSnapshot("test_counter", 1, 1);
  addHostCounterToSnapshot("test_host_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addGaugeToSnapshot("test_host_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 5);
  expectGauge(metricAt(0, metrics), getTagExtractedName("prefix.test_gauge"), 1);
  expectGauge(metricAt(1, metrics), getTagExtractedName("prefix.test_host_gauge"), 1);
  expectSum(metricAt(2, metrics), getTagExtractedName("prefix.test_counter"), 1, false);
  expectSum(metricAt(3, metrics), getTagExtractedName("prefix.test_host_counter"), 1, false);
  expectHistogram(metricAt(4, metrics), getTagExtractedName("prefix.test_histogram"), false);
}

TEST_F(OtlpMetricsFlusherTests, MetricsWithNoTaggedName) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, true, false));

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 3);
  expectGauge(metricAt(0, metrics), "test_gauge", 1);
  expectSum(metricAt(1, metrics), "test_counter", 1, false);
  expectHistogram(metricAt(2, metrics), "test_histogram", false);
}

TEST_F(OtlpMetricsFlusherTests, MetricsWithNoAttributes) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, false, false, true));

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 3);

  {
    const auto metric = metricAt(0, metrics);
    expectGauge(metric, getTagExtractedName("test_gauge"), 1);
    expectNoAttributes(metric.gauge().data_points()[0].attributes());
  }

  {
    const auto metric = metricAt(1, metrics);
    expectSum(metric, getTagExtractedName("test_counter"), 1, false);
    expectNoAttributes(metric.sum().data_points()[0].attributes());
  }

  {
    const auto metric = metricAt(2, metrics);
    expectHistogram(metric, getTagExtractedName("test_histogram"), false);
    expectNoAttributes(metric.histogram().data_points()[0].attributes());
  }
}

TEST_F(OtlpMetricsFlusherTests, GaugeMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addGaugeToSnapshot("test_gauge1", 1);
  addGaugeToSnapshot("test_gauge2", 2);
  addHostGaugeToSnapshot("test_host_gauge1", 3);
  addHostGaugeToSnapshot("test_host_gauge2", 4);

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 4);
  expectGauge(metricAt(0, metrics), getTagExtractedName("test_gauge1"), 1);
  expectGauge(metricAt(1, metrics), getTagExtractedName("test_gauge2"), 2);
  expectGauge(metricAt(2, metrics), getTagExtractedName("test_host_gauge1"), 3);
  expectGauge(metricAt(3, metrics), getTagExtractedName("test_host_gauge2"), 4);
}

TEST_F(OtlpMetricsFlusherTests, CumulativeCounterMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addCounterToSnapshot("test_counter1", 1, 1);
  addCounterToSnapshot("test_counter2", 2, 3);
  addHostCounterToSnapshot("test_host_counter1", 2, 4);
  addHostCounterToSnapshot("test_host_counter2", 5, 10);

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 4);
  expectSum(metricAt(0, metrics), getTagExtractedName("test_counter1"), 1, false);
  expectSum(metricAt(1, metrics), getTagExtractedName("test_counter2"), 3, false);
  expectSum(metricAt(2, metrics), getTagExtractedName("test_host_counter1"), 4, false);
  expectSum(metricAt(3, metrics), getTagExtractedName("test_host_counter2"), 10, false);
}

TEST_F(OtlpMetricsFlusherTests, DeltaCounterMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(true, false, true, true));

  addCounterToSnapshot("test_counter1", 1, 1);
  addCounterToSnapshot("test_counter2", 2, 3);
  addHostCounterToSnapshot("test_host_counter1", 2, 4);
  addHostCounterToSnapshot("test_host_counter2", 5, 10);

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 4);
  expectSum(metricAt(0, metrics), getTagExtractedName("test_counter1"), 1, true);
  expectSum(metricAt(1, metrics), getTagExtractedName("test_counter2"), 2, true);
  expectSum(metricAt(2, metrics), getTagExtractedName("test_host_counter1"), 2, true);
  expectSum(metricAt(3, metrics), getTagExtractedName("test_host_counter2"), 5, true);
}

TEST_F(OtlpMetricsFlusherTests, CumulativeHistogramMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions());

  addHistogramToSnapshot("test_histogram1");
  addHistogramToSnapshot("test_histogram2");

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 2);
  expectHistogram(metricAt(0, metrics), getTagExtractedName("test_histogram1"), false);
  expectHistogram(metricAt(1, metrics), getTagExtractedName("test_histogram2"), false);
}

TEST_F(OtlpMetricsFlusherTests, DeltaHistogramMetric) {
  OtlpMetricsFlusherImpl flusher(otlpOptions(false, true, true, true));

  addHistogramToSnapshot("test_histogram1", true);
  addHistogramToSnapshot("test_histogram2", true);

  MetricsExportRequestSharedPtr metrics = flusher.flush(snapshot_);
  expectMetricsCount(metrics, 2);
  expectHistogram(metricAt(0, metrics), getTagExtractedName("test_histogram1"), true);
  expectHistogram(metricAt(1, metrics), getTagExtractedName("test_histogram2"), true);
}

class MockOpenTelemetryGrpcMetricsExporter : public OpenTelemetryGrpcMetricsExporter {
public:
  MOCK_METHOD(void, send, (MetricsExportRequestPtr &&));
  MOCK_METHOD(void, onSuccess, (Grpc::ResponsePtr<MetricsExportResponse>&&, Tracing::Span&));
  MOCK_METHOD(void, onFailure, (Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&));
};

class MockOtlpMetricsFlusher : public OtlpMetricsFlusher {
public:
  MOCK_METHOD(MetricsExportRequestPtr, flush, (Stats::MetricSnapshot&), (const));
};

class OpenTelemetryGrpcSinkTests : public OpenTelemetryStatsSinkTests {
public:
  OpenTelemetryGrpcSinkTests()
      : flusher_(std::make_shared<MockOtlpMetricsFlusher>()),
        exporter_(std::make_shared<MockOpenTelemetryGrpcMetricsExporter>()) {}

  const std::shared_ptr<MockOtlpMetricsFlusher> flusher_;
  const std::shared_ptr<MockOpenTelemetryGrpcMetricsExporter> exporter_;
};

TEST_F(OpenTelemetryGrpcSinkTests, BasicFlow) {
  MetricsExportRequestPtr request = std::make_unique<MetricsExportRequest>();
  EXPECT_CALL(*flusher_, flush(_)).WillOnce(Return(ByMove(std::move(request))));
  EXPECT_CALL(*exporter_, send(_));

  OpenTelemetryGrpcSink sink(flusher_, exporter_);
  sink.flush(snapshot_);
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
