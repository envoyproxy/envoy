#pragma once

#include <memory>

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Sdk {
namespace Trace {

/**
 * OpenTelemetry trace signal type aliases.
 *
 * Origin: All types are derived from official OpenTelemetry C++ SDK and protocol definitions
 * Reference: https://github.com/open-telemetry/opentelemetry-cpp
 */

/**
 * @brief The type of the span (from OpenTelemetry specification)
 * @see
 * https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/api.md#spankind
 */
using OTelSpanKind = ::opentelemetry::proto::trace::v1::Span::SpanKind;

// OTLP export request/response types for traces (from OTLP protocol)
using ExportRequest = ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;
using ExportResponse = ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;

// Smart pointer aliases for trace export requests (Envoy convenience types)
using ExportRequestPtr = std::unique_ptr<ExportRequest>;
using ExportRequestSharedPtr = std::shared_ptr<ExportRequest>;

} // namespace Trace
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
