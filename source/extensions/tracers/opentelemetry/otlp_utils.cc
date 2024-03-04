#include "source/extensions/tracers/opentelemetry/otlp_utils.h"

#include <cstdint>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

void OtlpUtils::populateAnyValue(opentelemetry::proto::common::v1::AnyValue& value_proto,
                                 const OTelAttribute& attribute_value) {
  switch (attribute_value.index()) {
  case opentelemetry::common::AttributeType::kTypeBool:
    value_proto.set_bool_value(opentelemetry::nostd::get<bool>(attribute_value) ? true : false);
    break;
  case opentelemetry::common::AttributeType::kTypeInt:
    value_proto.set_int_value(opentelemetry::nostd::get<int32_t>(attribute_value));
    break;
  case opentelemetry::common::AttributeType::kTypeInt64:
    value_proto.set_int_value(opentelemetry::nostd::get<int64_t>(attribute_value));
    break;
  case opentelemetry::common::AttributeType::kTypeUInt:
    value_proto.set_int_value(opentelemetry::nostd::get<uint32_t>(attribute_value));
    break;
  case opentelemetry::common::AttributeType::kTypeUInt64:
    value_proto.set_int_value(opentelemetry::nostd::get<uint64_t>(attribute_value));
    break;
  case opentelemetry::common::AttributeType::kTypeDouble:
    value_proto.set_double_value(opentelemetry::nostd::get<double>(attribute_value));
    break;
  case opentelemetry::common::AttributeType::kTypeCString:
    value_proto.set_string_value(opentelemetry::nostd::get<const char*>(attribute_value));
    break;
  case opentelemetry::common::AttributeType::kTypeString: {
    value_proto.set_string_value(
        opentelemetry::nostd::get<opentelemetry::nostd::string_view>(attribute_value).data(),
        opentelemetry::nostd::get<opentelemetry::nostd::string_view>(attribute_value).size());
  } break;
  default:
    return;
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
