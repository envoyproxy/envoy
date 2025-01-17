#pragma once

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/tracers/opentelemetry/trace_exporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

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

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
