#pragma once

#include "source/extensions/common/opentelemetry/exporters/otlp/populate_attribute_utils.h"
#include "source/extensions/common/opentelemetry/types.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Import shared types into the tracer namespace for backward compatibility.
using OTelSpanKind = ::Envoy::Extensions::OpenTelemetry::OTelSpanKind;
using OTelAttribute = ::Envoy::Extensions::OpenTelemetry::OTelAttribute;
using OtelAttributes = ::Envoy::Extensions::OpenTelemetry::OtelAttributes;
using OtlpUtils = ::Envoy::Extensions::OpenTelemetry::Exporters::Otlp::PopulateAttributeUtils;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
