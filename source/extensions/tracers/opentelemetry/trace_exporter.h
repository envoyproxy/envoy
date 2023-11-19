#pragma once

#include "source/common/common/logger.h"

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

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
};

using OpenTelemetryTraceExporterPtr = std::unique_ptr<OpenTelemetryTraceExporter>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
