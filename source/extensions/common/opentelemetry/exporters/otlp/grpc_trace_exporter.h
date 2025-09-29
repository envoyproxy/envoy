#pragma once

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/trace_exporter.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Exporters {
namespace OTLP {

/**
 * Exporter client for OTLP Traces. Provides abstraction on top of gRPC stream.
 */
class OpenTelemetryGrpcTraceExporter
    : public OpenTelemetryTraceExporter,
      public Grpc::AsyncRequestCallbacks<ExportTraceServiceResponse> {
public:
  OpenTelemetryGrpcTraceExporter(const Grpc::RawAsyncClientSharedPtr& client);
  ~OpenTelemetryGrpcTraceExporter() override = default;

  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;

  void onSuccess(Grpc::ResponsePtr<ExportTraceServiceResponse>&& response, Tracing::Span&) override;

  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span&) override;

  bool log(const ExportTraceServiceRequest& request) override;

  Grpc::AsyncClient<ExportTraceServiceRequest, ExportTraceServiceResponse> client_;
  const Protobuf::MethodDescriptor& service_method_;
};

} // namespace OTLP
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
