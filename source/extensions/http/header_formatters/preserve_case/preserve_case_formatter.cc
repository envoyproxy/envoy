#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

PreserveCaseHeaderFormatter::PreserveCaseHeaderFormatter(
    const bool forward_reason_phrase,
    const envoy::extensions::http::header_formatters::preserve_case::v3::
        PreserveCaseFormatterConfig::FormatterTypeOnEnvoyHeaders formatter_type_on_envoy_headers)
    : forward_reason_phrase_(forward_reason_phrase),
      formatter_type_on_envoy_headers_(formatter_type_on_envoy_headers) {
  switch (formatter_type_on_envoy_headers_) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      DEFAULT:
    header_key_formatter_on_enovy_headers_ = Envoy::Http::HeaderKeyFormatterConstPtr();
    break;
  case envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      PROPER_CASE:
    header_key_formatter_on_enovy_headers_ =
        std::make_unique<Envoy::Http::Http1::ProperCaseHeaderKeyFormatter>();
    break;
  }
}

std::string PreserveCaseHeaderFormatter::format(absl::string_view key) const {
  const auto remembered_key_itr = original_header_keys_.find(key);
  // TODO(mattklein123): We can avoid string copies here if the formatter interface allowed us
  // to return something like GetAllOfHeaderAsStringResult with both a string_view and an
  // optional backing string. We can do this in a follow up if there is interest.
  if (remembered_key_itr != original_header_keys_.end()) {
    return *remembered_key_itr;
  } else if (formatterOnEnvoyHeaders().has_value()) {
    return formatterOnEnvoyHeaders()->format(key);
  } else {
    return std::string(key);
  }
}

void PreserveCaseHeaderFormatter::processKey(absl::string_view key) {
  // Note: This implementation will only remember the first instance of a particular header key.
  // So for example "Foo" followed by "foo" will both be serialized as "Foo" on the way out. We
  // could do better here but it's unlikely it's worth it and we can see if anyone complains about
  // the implementation.
  original_header_keys_.emplace(key);
}

void PreserveCaseHeaderFormatter::setReasonPhrase(absl::string_view reason_phrase) {
  if (forward_reason_phrase_) {
    reason_phrase_ = std::string(reason_phrase);
  }
};

absl::string_view PreserveCaseHeaderFormatter::getReasonPhrase() const { return {reason_phrase_}; };

Envoy::Http::HeaderKeyFormatterOptConstRef
PreserveCaseHeaderFormatter::formatterOnEnvoyHeaders() const {
  return makeOptRefFromPtr(header_key_formatter_on_enovy_headers_.get());
}

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
