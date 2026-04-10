#include "source/extensions/common/opentelemetry/exporters/otlp/grpc_trace_exporter.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/status.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/environment.h"
#include "source/extensions/common/opentelemetry/sdk/trace/constants.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

OtlpGrpcTraceExporter::OtlpGrpcTraceExporter(const Grpc::RawAsyncClientSharedPtr& client)
    : client_(client),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(std::string(
          Envoy::Extensions::OpenTelemetry::Sdk::Trace::Constants::kTraceServiceExportMethod))) {}

void OtlpGrpcTraceExporter::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  metadata.setReferenceUserAgent(GetUserAgent());
}

void OtlpGrpcTraceExporter::onSuccess(
    Grpc::ResponsePtr<opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse>&&
        response,
    Tracing::Span&) {
  if (response->has_partial_success()) {
    auto msg = response->partial_success().error_message();
    auto rejected_spans = response->partial_success().rejected_spans();
    if (rejected_spans > 0 || !msg.empty()) {
      if (msg.empty()) {
        msg = "empty message";
      }
      ENVOY_LOG(debug, "OTLP partial success: {} ({} spans rejected)", msg, rejected_spans);
    }
  }
}

void OtlpGrpcTraceExporter::onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                                      Tracing::Span&) {
  ENVOY_LOG(debug, "OTLP trace export failed with status: {}, message: {}",
            Grpc::Utility::grpcStatusToString(status), message);
}

bool OtlpGrpcTraceExporter::log(
    const opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest& request) {
  client_->send(service_method_, request, *this, Tracing::NullSpan::instance(),
                Http::AsyncClient::RequestOptions());
  OtlpTraceExporter::logExportedSpans(request);
  return true;
}

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
