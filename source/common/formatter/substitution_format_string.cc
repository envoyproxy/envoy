#include "common/formatter/substitution_format_string.h"

#include "envoy/api/api.h"

#include "common/config/datasource.h"
#include "common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

FormatterPtr
SubstitutionFormatStringUtils::createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                                   bool preserve_types, bool omit_empty_values) {
  return std::make_unique<JsonFormatterImpl>(struct_format, preserve_types, omit_empty_values);
}

FormatterPtr SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config, Api::Api& api) {
  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return std::make_unique<FormatterImpl>(config.text_format(), config.omit_empty_values());
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat: {
    return createJsonFormatter(config.json_format(), true, config.omit_empty_values());
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormatSource:
    return std::make_unique<FormatterImpl>(
        Config::DataSource::read(config.text_format_source(), true, api));
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return nullptr;
}

} // namespace Formatter
} // namespace Envoy
