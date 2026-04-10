#pragma once

#include "source/extensions/common/opentelemetry/sdk/common/types.h"

#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {

/**
 * @brief The type of the span.
 * see
 * https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/api.md#spankind
 */
using OTelSpanKind = ::opentelemetry::proto::trace::v1::Span::SpanKind;
using OTelAttribute = Sdk::Common::AttributeValue;
using OtelAttributes = Sdk::Common::OwnedAttributeMap;

using KeyValue = ::opentelemetry::proto::common::v1::KeyValue;
using AnyValue = ::opentelemetry::proto::common::v1::AnyValue;

} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
