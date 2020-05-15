#include "common/common/substitution_format_string.h"

#include "common/access_log/access_log_formatter.h"

namespace Envoy {
namespace {

absl::flat_hash_map<std::string, std::string>
convertJsonFormatToMap(const ProtobufWkt::Struct& json_format) {
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

AccessLog::FormatterPtr
SubstitutionFormatStringUtils::createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                                   bool preserve_types) {
  auto json_format_map = convertJsonFormatToMap(struct_format);
  return std::make_unique<AccessLog::JsonFormatterImpl>(json_format_map, preserve_types);
}

AccessLog::FormatterPtr SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config) {
  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return std::make_unique<AccessLog::FormatterImpl>(config.text_format());
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat: {
    return createJsonFormatter(config.json_format(), true);
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return nullptr;
}

} // namespace Envoy
