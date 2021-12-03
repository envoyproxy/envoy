#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"
#include "grpc_trace_exporter.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

OpenTelemetryGrpcTraceExporter::OpenTelemetryGrpcTraceExporter(
    const Grpc::RawAsyncClientSharedPtr& client, std::string trace_name)
    : client_(client, *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                          "opentelemetry.proto.collector.trace.v1.TraceService.Export")),
      trace_name_(trace_name) {}

bool OpenTelemetryGrpcTraceExporter::log(const ExportTraceServiceRequest& request) {
  return client_.log(request);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy