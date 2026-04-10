#pragma once

#include "source/extensions/common/opentelemetry/exporters/otlp/trace_exporter.h"

using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using OpenTelemetryTraceExporter =
    ::Envoy::Extensions::OpenTelemetry::Exporters::Otlp::OtlpTraceExporter;
using OpenTelemetryTraceExporterPtr =
    ::Envoy::Extensions::OpenTelemetry::Exporters::Otlp::OtlpTraceExporterPtr;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
