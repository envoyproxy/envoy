#pragma once

#include <memory>

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Sdk {
namespace Logs {

/**
 * OpenTelemetry logs signal type aliases.
 *
 * Origin: All types are derived from official OpenTelemetry protocol definitions
 * Reference: https://github.com/open-telemetry/opentelemetry-cpp
 */

// OTLP export request/response types for logs (from OTLP protocol)
using ExportRequest = ::opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest;
using ExportResponse = ::opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse;

// Smart pointer aliases for logs export requests (Envoy convenience types)
using ExportRequestPtr = std::unique_ptr<ExportRequest>;
using ExportRequestSharedPtr = std::shared_ptr<ExportRequest>;

} // namespace Logs
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
