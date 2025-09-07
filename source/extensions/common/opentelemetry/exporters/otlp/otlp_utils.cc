#include "source/extensions/common/opentelemetry/exporters/otlp/otlp_utils.h"

#include <cstdint>
#include <string>

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Exporters {
namespace OTLP {

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
    const auto sv = opentelemetry::nostd::get<opentelemetry::nostd::string_view>(attribute_value);
    value_proto.set_string_value(sv.data(), sv.size());
    break;
  }
  default:
    return;
  }
}

} // namespace OTLP
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
