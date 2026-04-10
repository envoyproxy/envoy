#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/version/version.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/grpc_trace_exporter.h"

#include "test/mocks/grpc/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

using testing::_;
using testing::Invoke;

using ExportTraceServiceResponse =
    opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;

class OtlpGrpcTraceExporterTest : public testing::Test {
public:
  OtlpGrpcTraceExporterTest() : async_client_(new Grpc::MockAsyncClient) {}

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

// Verify that log() forwards the request to the gRPC client and returns true.
// Also checks that onCreateInitialMetadata sets the OTLP user-agent.
TEST_F(OtlpGrpcTraceExporterTest, CreateExporterAndExportSpan) {
  OtlpGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});

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

// Verify that onFailure logs at debug level.
TEST_F(OtlpGrpcTraceExporterTest, OnFailureLogsDebug) {
  OtlpGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});

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
  EXPECT_LOG_CONTAINS("debug", "OTLP trace export failed",
                      exporter.onFailure(Grpc::Status::Internal, "bad", null_span));
}

// Verify all branches of onSuccess for partial_success handling.
TEST_F(OtlpGrpcTraceExporterTest, OnSuccessPartialSuccess) {
  OtlpGrpcTraceExporter exporter(Grpc::RawAsyncClientPtr{async_client_});
  auto null_span = Tracing::NullSpan();

  // partial_success with error_message only.
  auto response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success()->set_error_message("test error");
  EXPECT_LOG_CONTAINS("debug", "OTLP partial success: test error (0 spans rejected)",
                      exporter.onSuccess(std::move(response), null_span));

  // partial_success with error_message and rejected_spans.
  response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success()->set_error_message("test error 2");
  response->mutable_partial_success()->set_rejected_spans(10);
  EXPECT_LOG_CONTAINS("debug", "OTLP partial success: test error 2 (10 spans rejected)",
                      exporter.onSuccess(std::move(response), null_span));

  // partial_success with rejected_spans only — empty message fallback.
  response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success()->set_rejected_spans(5);
  EXPECT_LOG_CONTAINS("debug", "OTLP partial success: empty message (5 spans rejected)",
                      exporter.onSuccess(std::move(response), null_span));

  // partial_success set but neither message nor rejected_spans — no log.
  response = std::make_unique<ExportTraceServiceResponse>();
  response->mutable_partial_success();
  EXPECT_LOG_NOT_CONTAINS("debug", "OTLP partial success",
                          exporter.onSuccess(std::move(response), null_span));

  // No partial_success at all — no log.
  response = std::make_unique<ExportTraceServiceResponse>();
  EXPECT_LOG_NOT_CONTAINS("debug", "OTLP partial success",
                          exporter.onSuccess(std::move(response), null_span));
}

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
