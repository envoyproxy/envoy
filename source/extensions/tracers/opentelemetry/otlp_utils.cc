#include "source/extensions/tracers/opentelemetry/otlp_utils.h"

#include <cstdint>
#include <string>

#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/version/version.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

enum OTelAttributeType {
  KTypeBool,
  KTypeInt,
  KTypeUInt,
  KTypeInt64,
  KTypeDouble,
  KTypeString,
  KTypeStringView,
  KTypeSpanBool,
  KTypeSpanInt,
  KTypeSpanUInt,
  KTypeSpanInt64,
  KTypeSpanDouble,
  KTypeSpanString,
  KTypeSpanStringView,
  KTypeUInt64,
  KTypeSpanUInt64,
  KTypeSpanByte
};

const std::string& OtlpUtils::getOtlpUserAgentHeader() {
  CONSTRUCT_ON_FIRST_USE(std::string,
                         fmt::format("OTel-OTLP-Exporter-Envoy/{}", Envoy::VersionInfo::version()));
}

void OtlpUtils::populateAnyValue(opentelemetry::proto::common::v1::AnyValue& value_proto,
                                 const OTelAttribute& attribute_value) {
  switch (attribute_value.index()) {
  case OTelAttributeType::KTypeBool:
    value_proto.set_bool_value(opentelemetry::nostd::get<bool>(attribute_value) ? true : false);
    break;
  case OTelAttributeType::KTypeInt:
    value_proto.set_int_value(opentelemetry::nostd::get<int32_t>(attribute_value));
    break;
  case OTelAttributeType::KTypeInt64:
    value_proto.set_int_value(opentelemetry::nostd::get<int64_t>(attribute_value));
    break;
  case OTelAttributeType::KTypeUInt:
    value_proto.set_int_value(opentelemetry::nostd::get<uint32_t>(attribute_value));
    break;
  case OTelAttributeType::KTypeUInt64:
    value_proto.set_int_value(opentelemetry::nostd::get<uint64_t>(attribute_value));
    break;
  case OTelAttributeType::KTypeDouble:
    value_proto.set_double_value(opentelemetry::nostd::get<double>(attribute_value));
    break;
  case OTelAttributeType::KTypeString: {
    const auto sv = opentelemetry::nostd::get<std::string>(attribute_value);
    value_proto.set_string_value(sv.data(), sv.size());
    break;
  }
  case OTelAttributeType::KTypeStringView: {
    const auto sv = opentelemetry::nostd::get<absl::string_view>(attribute_value);
    value_proto.set_string_value(sv.data(), sv.size());
    break;
  }
  case OTelAttributeType::KTypeSpanString: {
    auto array_value = value_proto.mutable_array_value();
    for (const auto& val : opentelemetry::nostd::get<std::vector<std::string>>(attribute_value)) {
      array_value->add_values()->set_string_value(val.data(), val.size());
    }
    break;
  }
  case OTelAttributeType::KTypeSpanStringView: {
    auto array_value = value_proto.mutable_array_value();
    for (const auto& val :
         opentelemetry::nostd::get<std::vector<absl::string_view>>(attribute_value)) {
      array_value->add_values()->set_string_value(val.data(), val.size());
    }
    break;
  }
  default:
    IS_ENVOY_BUG("unexpected otel attribute type");
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
