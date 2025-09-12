#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/common/opentelemetry/sdk/trace/types.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Exporters {
namespace OTLP {

// Import trace types from SDK
using namespace Envoy::Extensions::Common::OpenTelemetry::Sdk::Trace;

// Type aliases for backward compatibility
using ExportTraceServiceRequest = ExportRequest;
using ExportTraceServiceResponse = ExportResponse;

/**
 * @brief Base class for all OpenTelemetry Protocol (OTLP) exporters.
 * @see
 * https://github.com/open-telemetry/opentelemetry-proto/blob/v1.0.0/docs/specification.md#otlphttp
 */
class OpenTelemetryTraceExporter : public Logger::Loggable<Logger::Id::tracing> {
public:
  virtual ~OpenTelemetryTraceExporter() = default;

  /**
   * @brief Exports the trace request to the configured OTLP service.
   *
   * @param request The protobuf-encoded OTLP trace request.
   * @return true When the request was sent.
   * @return false When sending the request failed.
   */
  virtual bool log(const ExportTraceServiceRequest& request) = 0;

  /**
   * @brief Logs as debug the number of exported spans.
   *
   * @param request The protobuf-encoded OTLP trace request.
   */
  void logExportedSpans(const ExportTraceServiceRequest& request) {
    if (request.resource_spans(0).has_resource()) {
      if (request.resource_spans(0).scope_spans(0).has_scope()) {
        ENVOY_LOG(debug, "Number of exported spans: {}",
                  request.resource_spans(0).scope_spans(0).spans_size());
      }
    }
  }
};

using OpenTelemetryTraceExporterPtr = std::unique_ptr<OpenTelemetryTraceExporter>;

} // namespace OTLP
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
