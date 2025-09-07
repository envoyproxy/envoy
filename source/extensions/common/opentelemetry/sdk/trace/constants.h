#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Sdk {
namespace Trace {

/**
 * Constants for OpenTelemetry OTLP trace signal.
 * These constants ensure consistency across trace exporters and tracers.
 *
 * Origin: All constants are derived from official OpenTelemetry Protocol (OTLP) specification
 * Reference:
 * https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/protocol/otlp.md
 */
class Constants {
public:
  // gRPC service method for trace exports (from OTLP spec)
  static constexpr absl::string_view TRACE_SERVICE_EXPORT_METHOD =
      "opentelemetry.proto.collector.trace.v1.TraceService.Export";

  // HTTP endpoint for trace exports (from OTLP spec)
  static constexpr absl::string_view DEFAULT_OTLP_TRACES_ENDPOINT = "/v1/traces";
};

} // namespace Trace
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
