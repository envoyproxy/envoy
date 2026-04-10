#pragma once

#include <memory>

#include "source/common/common/logger.h"

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

class OtlpTraceExporter : public Logger::Loggable<Logger::Id::tracing> {
public:
  virtual ~OtlpTraceExporter() = default;

  virtual bool
  log(const opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest& request) = 0;

  void logExportedSpans(
      const opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest& request) {
    if (request.resource_spans(0).has_resource()) {
      if (request.resource_spans(0).scope_spans(0).has_scope()) {
        ENVOY_LOG(debug, "Number of exported spans: {}",
                  request.resource_spans(0).scope_spans(0).spans_size());
      }
    }
  }
};

using OtlpTraceExporterPtr = std::unique_ptr<OtlpTraceExporter>;

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
