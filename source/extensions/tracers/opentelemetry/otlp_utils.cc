#include "source/extensions/tracers/opentelemetry/otlp_utils.h"

#include <cstdint>
#include <string>

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/version/version.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

const std::string& OtlpUtils::getOtlpUserAgentHeader() {
  CONSTRUCT_ON_FIRST_USE(std::string,
                         fmt::format("OTel-OTLP-Exporter-Envoy/{}", Envoy::VersionInfo::version()));
}

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

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
