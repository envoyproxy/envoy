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
  OpenTelemetryStatsSinkTests()
    : default_options_(std::make_shared<OtlpOptions>(
          envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig())) {}

  const OtlpOptionsSharedPtr& defaultOptions() {
    return default_options_;
  }

  void addCounterToSnapshot(
      NiceMock<Stats::MockMetricSnapshot>& snapshot,
      std::vector<std::unique_ptr<NiceMock<Stats::MockCounter>>>& counter_storage,
      const std::string& name, uint64_t delta, uint64_t value, bool used = true) {
    counter_storage.emplace_back(std::make_unique<NiceMock<Stats::MockCounter>>());
    counter_storage.back()->name_ = name;
    counter_storage.back()->value_ = value;
    counter_storage.back()->used_ = used;

    snapshot.counters_.push_back({delta, *counter_storage.back()});
  }

  void addGaugeToSnapshot(
        NiceMock<Stats::MockMetricSnapshot>& snapshot,
        std::vector<std::unique_ptr<NiceMock<Stats::MockGauge>>>& gauge_storage,
        const std::string& name, uint64_t value, bool used = true) {
    gauge_storage.emplace_back(std::make_unique<NiceMock<Stats::MockGauge>>());
    gauge_storage.back()->name_ = name;
    gauge_storage.back()->value_ = value;
    gauge_storage.back()->used_ = used;

    snapshot.gauges_.push_back(*gauge_storage.back());
  }

  void addHistogramToSnapshot(
        NiceMock<Stats::MockMetricSnapshot>& snapshot,
        std::vector<std::unique_ptr<NiceMock<Stats::MockParentHistogram>>>& histogram_storage,
        const std::string& name, bool used = true) {
    histogram_storage.emplace_back(std::make_unique<NiceMock<Stats::MockParentHistogram>>());
    histogram_storage.back()->name_ = name;
    histogram_storage.back()->used_ = used;

    snapshot.histograms_.push_back(*histogram_storage.back());
  }

  const OtlpOptionsSharedPtr default_options_;
};

class OpenTelemetryGrpcMetricsExporterImplTest : public OpenTelemetryStatsSinkTests {
public:
  OpenTelemetryGrpcMetricsExporterImplTest() {
    exporter_ = std::make_unique<OpenTelemetryGrpcMetricsExporterImpl>(
        defaultOptions(), Grpc::RawAsyncClientSharedPtr{&async_client_});
  }

  NiceMock<Grpc::MockAsyncClient> async_client_;
  OpenTelemetryGrpcMetricsExporterImplPtr exporter_;
};

TEST_F(OpenTelemetryGrpcMetricsExporterImplTest, NoExportRequest) {
  EXPECT_CALL(async_client_, sendRaw(_, _, _, _, _, _)).Times(0);
  exporter_->send(nullptr);
}

TEST_F(OpenTelemetryGrpcMetricsExporterImplTest, SendExportRequest) {
  EXPECT_CALL(async_client_, sendRaw(_, _, _, _, _, _));
  exporter_->send(std::make_unique<MetricsExportRequest>());
}

class MockOpenTelemetryGrpcMetricsExporter : public OpenTelemetryGrpcMetricsExporter {
public:
  MockOpenTelemetryGrpcMetricsExporter(const OtlpOptionsSharedPtr config,
                                       const Grpc::RawAsyncClientSharedPtr& raw_async_client)
      : OpenTelemetryGrpcMetricsExporter(config, raw_async_client) {}

  // OpenTelemetryGrpcMetricsExporter
  MOCK_METHOD(void, send, (MetricsExportRequestPtr&&));
};

class OpenTelemetryGrpcSinkTest : public OpenTelemetryStatsSinkTests {
public:
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockCounter>>> counter_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockGauge>>> gauge_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockParentHistogram>>> histogram_storage_;
};

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
