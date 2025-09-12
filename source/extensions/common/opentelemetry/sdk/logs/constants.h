#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Sdk {
namespace Logs {

/**
 * Constants for OpenTelemetry OTLP logs signal.
 * These constants ensure consistency across logs exporters and access loggers.
 *
 * Origin: All constants are derived from official OpenTelemetry Protocol (OTLP) specification
 * Reference:
 * https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/protocol/otlp.md
 */
class Constants {
public:
  // gRPC service method for logs exports (from OTLP spec)
  static constexpr absl::string_view LOGS_SERVICE_EXPORT_METHOD =
      "opentelemetry.proto.collector.logs.v1.LogsService.Export";

  // HTTP endpoint for logs exports (from OTLP spec)
  static constexpr absl::string_view DEFAULT_OTLP_LOGS_ENDPOINT = "/v1/logs";
};

} // namespace Logs
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
