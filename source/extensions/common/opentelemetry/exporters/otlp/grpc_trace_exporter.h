#pragma once

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/trace_exporter.h"

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

class OtlpGrpcTraceExporter
    : public OtlpTraceExporter,
      public Grpc::AsyncRequestCallbacks<
          opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse> {
public:
  OtlpGrpcTraceExporter(const Grpc::RawAsyncClientSharedPtr& client);
  ~OtlpGrpcTraceExporter() override = default;

  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;

  void onSuccess(
      Grpc::ResponsePtr<opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse>&&
          response,
      Tracing::Span&) override;

  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span&) override;

  bool log(const opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest& request)
      override;

  Grpc::AsyncClient<opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest,
                    opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse>
      client_;
  const Protobuf::MethodDescriptor& service_method_;
};

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
