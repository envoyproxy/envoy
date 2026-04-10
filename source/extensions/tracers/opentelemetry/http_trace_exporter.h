#pragma once

#include "source/extensions/common/opentelemetry/exporters/otlp/http_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/trace_exporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using OpenTelemetryHttpTraceExporter =
    ::Envoy::Extensions::OpenTelemetry::Exporters::Otlp::OtlpHttpTraceExporter;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
