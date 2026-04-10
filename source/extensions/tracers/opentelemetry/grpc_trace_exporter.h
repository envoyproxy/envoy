#pragma once

#include "source/extensions/common/opentelemetry/exporters/otlp/grpc_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/trace_exporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using OpenTelemetryGrpcTraceExporter =
    ::Envoy::Extensions::OpenTelemetry::Exporters::Otlp::OtlpGrpcTraceExporter;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
