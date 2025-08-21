#pragma once

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// This function validates that the method descriptors for gRPC services and type descriptors that
// are referenced in Any messages are available in the descriptor pool.
void validateProtoDescriptors();
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
