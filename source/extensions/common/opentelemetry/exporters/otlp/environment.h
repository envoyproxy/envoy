#pragma once

#include <string>

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

// Returns the User-Agent header value to use on OTLP exporter requests.
// The value is compliant with the OpenTelemetry specification:
// https://github.com/open-telemetry/opentelemetry-specification/blob/v1.30.0/specification/protocol/exporter.md#user-agent
const std::string& GetUserAgent();

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
