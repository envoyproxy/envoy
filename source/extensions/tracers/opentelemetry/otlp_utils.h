#pragma once

#include <string>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Contains utility functions  for Otel
 */
class OtlpUtils {

public:
  /**
   * @brief Get the User-Agent header value to be used on the OTLP exporter request.
   *
   * The header value is compliant with the OpenTelemetry specification. See:
   * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.30.0/specification/protocol/exporter.md#user-agent
   * @return std::string The User-Agent for the OTLP exporters in Envoy.
   */
  static const std::string& getOtlpUserAgentHeader();
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
