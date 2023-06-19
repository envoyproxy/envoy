#pragma once

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class OpenTelemetryTraceExporter : Logger::Loggable<Logger::Id::tracing> {
public:
  virtual ~OpenTelemetryTraceExporter() = default;

  virtual bool log(const ExportTraceServiceRequest& request) = 0;
};


using OpenTelemetryTraceExporterPtr = std::unique_ptr<OpenTelemetryTraceExporter>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy