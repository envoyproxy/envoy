#include "common/common/substitution_format_string.h"

#include "common/access_log/access_log_formatter.h"

namespace Envoy {
namespace {

absl::flat_hash_map<std::string, std::string>
convertJsonFormatToMap(ProtobufWkt::Struct json_format) {
  absl::flat_hash_map<std::string, std::string> output;
  for (const auto& pair : json_format.fields()) {
    if (pair.second.kind_case() != ProtobufWkt::Value::kStringValue) {
      throw EnvoyException("Only string values are supported in the JSON access log format.");
    }
    output.emplace(pair.first, pair.second.string_value());
  }
  return output;
}

} // namespace

AccessLog::FormatterPtr SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config) {
  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return std::make_unique<AccessLog::FormatterImpl>(config.text_format());
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat: {
    auto json_format_map = convertJsonFormatToMap(config.json_format());
    return std::make_unique<AccessLog::JsonFormatterImpl>(json_format_map, false);
  }
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTypedJsonFormat: {
    auto json_format_map = convertJsonFormatToMap(config.typed_json_format());
    return std::make_unique<AccessLog::JsonFormatterImpl>(json_format_map, true);
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return nullptr;
}

} // namespace Envoy
