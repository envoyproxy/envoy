#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Sdk {
namespace Metrics {

/**
 * Constants for OpenTelemetry OTLP metrics signal.
 * These constants ensure consistency across metrics exporters and stat sinks.
 *
 * Origin: All constants are derived from official OpenTelemetry Protocol (OTLP) specification
 * Reference:
 * https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/protocol/otlp.md
 */
class Constants {
public:
  // gRPC service method for metrics exports (from OTLP spec)
  static constexpr absl::string_view METRICS_SERVICE_EXPORT_METHOD =
      "opentelemetry.proto.collector.metrics.v1.MetricsService.Export";

  // HTTP endpoint for metrics exports (from OTLP spec)
  static constexpr absl::string_view DEFAULT_OTLP_METRICS_ENDPOINT = "/v1/metrics";
};

} // namespace Metrics
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
