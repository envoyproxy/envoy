#include "common/formatter/substitution_format_string.h"

#include "common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

FormatterPtr SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config) {
  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return std::make_unique<FormatterImpl>(config.text_format());
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat: {
    return std::make_unique<JsonFormatterImpl>(config.json_format(), true);
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return nullptr;
}

} // namespace Formatter
} // namespace Envoy
