#include <sys/types.h>

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/version/version.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::_;
using testing::Invoke;
using testing::Return;

class OpenTelemetryGrpcTraceExporterTest : public testing::Test {
public:
  using TraceCallbacks = Grpc::AsyncStreamCallbacks<
      opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse>;

  OpenTelemetryGrpcTraceExporterTest() : async_client_(new Grpc::MockAsyncClient) {
    expectTraceExportStart();
  }

  void expectTraceExportStart() {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
        .WillOnce(
            Invoke([this](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& cbs,
                          const Http::AsyncClient::StreamOptions&) {
              this->callbacks_ = dynamic_cast<TraceCallbacks*>(&cbs);
              return &this->conn_;
            }));
  }

  void expectTraceExportMessage(const std::string& expected_message_yaml) {
    opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(conn_, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
    EXPECT_CALL(conn_, sendMessageRaw_(_, true))
        .WillOnce(Invoke([expected_message](Buffer::InstancePtr& request, bool) {
          opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
        }));
  }

protected:
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncStream conn_;
  TraceCallbacks* callbacks_;
};

TEST_F(OpenTelemetryGrpcTraceExporterTest, CreateExporterAndExportSpan) {
  OpenTelemetryGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});

  expectTraceExportMessage(R"EOF(
    resource_spans:
      scope_spans:
        - spans:
          - name: "test"
  )EOF");
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(exporter.log(request));

  Http::TestRequestHeaderMapImpl metadata;
  callbacks_->onCreateInitialMetadata(metadata);
  EXPECT_EQ(metadata.getUserAgentValue(),
            "OTel-OTLP-Exporter-Envoy/" + Envoy::VersionInfo::version());
}

TEST_F(OpenTelemetryGrpcTraceExporterTest, NoExportWithHighWatermark) {
  OpenTelemetryGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});

  EXPECT_CALL(conn_, isAboveWriteBufferHighWatermark()).WillOnce(Return(true));
  EXPECT_CALL(conn_, sendMessageRaw_(_, false)).Times(0);
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("tests");
  *request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_FALSE(exporter.log(request));
}

TEST_F(OpenTelemetryGrpcTraceExporterTest, ExportWithRemoteClose) {
  OpenTelemetryGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});
  std::string request_yaml = R"EOF(
    resource_spans:
      scope_spans:
        - spans:
          - name: "test"
  )EOF";

  expectTraceExportMessage(request_yaml);
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(exporter.log(request));

  // Terminate the request, now that we've created it.
  callbacks_->onRemoteClose(Grpc::Status::Internal, "bad");

  // Second call should make a new request.
  expectTraceExportStart();
  expectTraceExportMessage(request_yaml);
  EXPECT_TRUE(exporter.log(request));
}

TEST_F(OpenTelemetryGrpcTraceExporterTest, ExportWithNoopCallbacks) {
  OpenTelemetryGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});
  expectTraceExportMessage(R"EOF(
    resource_spans:
      scope_spans:
        - spans:
          - name: "test"
  )EOF");
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(exporter.log(request));

  Http::TestRequestHeaderMapImpl metadata;
  callbacks_->onCreateInitialMetadata(metadata);
  callbacks_->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
  callbacks_->onReceiveTrailingMetadata(std::make_unique<Http::TestResponseTrailerMapImpl>());
  callbacks_->onReceiveMessage(
      std::make_unique<opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse>());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
