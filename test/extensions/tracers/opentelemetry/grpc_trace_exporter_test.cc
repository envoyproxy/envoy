#include <sys/types.h>

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/version/version.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"

#include "test/mocks/grpc/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::_;
using testing::Invoke;

class OpenTelemetryGrpcTraceExporterTest : public testing::Test {
public:
  OpenTelemetryGrpcTraceExporterTest() : async_client_(new Grpc::MockAsyncClient) {}

  void expectTraceExportMessage(const std::string& expected_message_yaml) {
    opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);

    EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
        .WillOnce(Invoke([expected_message,
                          this](absl::string_view, absl::string_view, Buffer::InstancePtr&& request,
                                Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                                const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
          opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
          return &async_request_;
        }));
  }

protected:
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncRequest async_request_;
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
  exporter.onCreateInitialMetadata(metadata);
  EXPECT_EQ(metadata.getUserAgentValue(),
            "OTel-OTLP-Exporter-Envoy/" + Envoy::VersionInfo::version());
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
  auto null_span = Tracing::NullSpan();
  exporter.onFailure(Grpc::Status::Internal, "bad", null_span);

  // Second call should make a new request.
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

  auto null_span = Tracing::NullSpan();
  Http::TestRequestHeaderMapImpl metadata;
  exporter.onCreateInitialMetadata(metadata);
  exporter.onSuccess(
      std::make_unique<opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse>(),
      null_span);
}

TEST_F(OpenTelemetryGrpcTraceExporterTest, ExportPartialSuccess) {
  OpenTelemetryGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});
  auto null_span = Tracing::NullSpan();

  auto response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success()->set_error_message("test error");

  EXPECT_LOG_CONTAINS("debug", "OTLP partial success: test error (0 spans rejected)",
                      exporter.onSuccess(std::move(response), null_span));

  response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success()->set_error_message("test error 2");
  response->mutable_partial_success()->set_rejected_spans(10);

  EXPECT_LOG_CONTAINS("debug", "OTLP partial success: test error 2 (10 spans rejected)",
                      exporter.onSuccess(std::move(response), null_span));

  response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success()->set_rejected_spans(5);

  EXPECT_LOG_CONTAINS("debug", "OTLP partial success: empty message (5 spans rejected)",
                      exporter.onSuccess(std::move(response), null_span));

  response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success();

  EXPECT_LOG_NOT_CONTAINS("debug", "OTLP partial success",
                          exporter.onSuccess(std::move(response), null_span));
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
