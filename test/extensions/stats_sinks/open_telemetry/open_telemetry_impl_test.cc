#include "envoy/grpc/async_client.h"

#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"

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
    : options_(std::make_shared<OtlpOptions>(
          envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig())) {}

  const OtlpOptionsSharedPtr& defaultOptions() {
    return options_;
  }

  const OtlpOptionsSharedPtr options_;
};

class OpenTelemetryGrpcMetricsExporterImplTest : public OpenTelemetryStatsSinkTests {
public:
  OpenTelemetryGrpcMetricsExporterImplTest() {
    exporter_ = std::make_unique<OpenTelemetryGrpcMetricsExporterImpl>(
        defaultOptions(), Grpc::RawAsyncClientSharedPtr{&async_client_});
  }

  //Grpc::MockAsyncClient* async_client_{new NiceMock<Grpc::MockAsyncClient>};
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

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
