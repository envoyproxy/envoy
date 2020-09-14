#include "common/formatter/substitution_format_string.h"

#include "common/formatter/substitution_formatter.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Formatter {

FormatterPtr
SubstitutionFormatStringUtils::createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                                   bool preserve_types, bool omit_empty_values) {
  return std::make_unique<JsonFormatterImpl>(struct_format, preserve_types, omit_empty_values);
}

FormatterPtr SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config) {
  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return std::make_unique<FormatterImpl>(config.text_format(), config.omit_empty_values());
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat: {
    return createJsonFormatter(config.json_format(), true, config.omit_empty_values());
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return nullptr;
}

absl::string_view SubstitutionFormatStringUtils::getContentType(
    const envoy::config::core::v3::SubstitutionFormatString& config) {
  const std::string contentType = config.content_type();
  if (contentType == Http::Headers::get().ContentTypeValues.TextEventStream)
    return Http::Headers::get().ContentTypeValues.TextEventStream;
  else if (contentType == Http::Headers::get().ContentTypeValues.TextUtf8)
    return Http::Headers::get().ContentTypeValues.TextUtf8;
  else if (contentType == Http::Headers::get().ContentTypeValues.Html)
    return Http::Headers::get().ContentTypeValues.Html;
  else
    return Http::Headers::get().ContentTypeValues.Text;
}

} // namespace Formatter
} // namespace Envoy
