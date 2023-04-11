#include "envoy/grpc/async_client.h"

#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stats/mocks.h"

using namespace std::chrono_literals;
using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {
namespace {

class OpenTelemetryStatsSinkTests : public testing::Test {
public:

  const OtlpOptionsSharedPtr otlpOptions() {
    return std::make_shared<OtlpOptions>(
        envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig());
  }

  void addCounterToSnapshot(const std::string& name, uint64_t delta, uint64_t value,
                            bool used = true) {
    counter_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockCounter>>());
    counter_storage_.back()->name_ = name;
    counter_storage_.back()->value_ = value;
    counter_storage_.back()->used_ = used;

    snapshot_.counters_.push_back({delta, *counter_storage_.back()});
  }

  void addGaugeToSnapshot(const std::string& name, uint64_t value, bool used = true) {
    gauge_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockGauge>>());
    gauge_storage_.back()->name_ = name;
    gauge_storage_.back()->value_ = value;
    gauge_storage_.back()->used_ = used;

    snapshot_.gauges_.push_back(*gauge_storage_.back());
  }

  void addHistogramToSnapshot(const std::string& name, bool used = true) {
    histogram_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockParentHistogram>>());
    histogram_storage_.back()->name_ = name;
    histogram_storage_.back()->used_ = used;

    snapshot_.histograms_.push_back(*histogram_storage_.back());
  }

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

TEST_F(OpenTelemetryGrpcMetricsExporterImplTest, NoExportRequest) {
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _)).Times(0);
  exporter_->send(nullptr);
}

TEST_F(OpenTelemetryGrpcMetricsExporterImplTest, SendExportRequest) {
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _));
  exporter_->send(std::make_unique<MetricsExportRequest>());
}

class MockOpenTelemetryGrpcMetricsExporter : public OpenTelemetryGrpcMetricsExporter {
public:
  MockOpenTelemetryGrpcMetricsExporter(const OtlpOptionsSharedPtr config,
                                       const Grpc::RawAsyncClientSharedPtr& raw_async_client)
      : OpenTelemetryGrpcMetricsExporter(config, raw_async_client) {}

  // OpenTelemetryGrpcMetricsExporter
  MOCK_METHOD(void, send, (MetricsExportRequestPtr &&));
  MOCK_METHOD(void, onSuccess, (Grpc::ResponsePtr<MetricsExportResponse>&&, Tracing::Span&));
  MOCK_METHOD(void, onFailure, (Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&));
};

class OpenTelemetryGrpcSinkTest : public OpenTelemetryStatsSinkTests {
public:
  void init(const OtlpOptionsSharedPtr& config) {
    exporter_ = std::make_shared<MockOpenTelemetryGrpcMetricsExporter>(
        config, Grpc::RawAsyncClientSharedPtr{async_client_});
  }

  Grpc::MockAsyncClient* async_client_{new NiceMock<Grpc::MockAsyncClient>};
  std::shared_ptr<MockOpenTelemetryGrpcMetricsExporter> exporter_;
};

TEST_F(OpenTelemetryGrpcSinkTest, ExportRequestMetricCount) {
  auto default_options = otlpOptions();
  init(default_options);
  OpenTelemetryGrpcSink sink(default_options, exporter_);

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  EXPECT_CALL(*exporter_, send(_)).WillOnce(Invoke([](MetricsExportRequestPtr&& metrics) {
    EXPECT_EQ(1, metrics->resource_metrics().size());
    EXPECT_EQ(1, metrics->resource_metrics()[0].scope_metrics().size());
    EXPECT_EQ(3, metrics->resource_metrics()[0].scope_metrics()[0].metrics().size());
  }));

  sink.flush(snapshot_);

  gauge_storage_.back()->used_ = false;
  EXPECT_CALL(*exporter_, send(_)).WillOnce(Invoke([](MetricsExportRequestPtr&& metrics) {
    EXPECT_EQ(1, metrics->resource_metrics().size());
    EXPECT_EQ(1, metrics->resource_metrics()[0].scope_metrics().size());
    EXPECT_EQ(2, metrics->resource_metrics()[0].scope_metrics()[0].metrics().size());
  }));

  sink.flush(snapshot_);
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
