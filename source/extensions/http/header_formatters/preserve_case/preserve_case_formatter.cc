#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

PreserveCaseHeaderFormatter::PreserveCaseHeaderFormatter(
    const bool forward_reason_phrase,
    const envoy::extensions::http::header_formatters::preserve_case::v3::
        PreserveCaseFormatterConfig::FormatterTypeOnUnknownHeaders
            formatter_type_on_unknown_headers)
    : forward_reason_phrase_(forward_reason_phrase),
      formatter_type_on_unknown_headers_(formatter_type_on_unknown_headers) {
  switch (formatter_type_on_unknown_headers_) {
  case envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      PROPER_CASE:
    header_key_formatter_on_unknown_headers_ =
        std::make_unique<Envoy::Http::Http1::ProperCaseHeaderKeyFormatter>();
    break;
  default:
    header_key_formatter_on_unknown_headers_ = Envoy::Http::HeaderKeyFormatterConstPtr();
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
  } else if (header_key_formatter_on_unknown_headers_.get() != nullptr) {
    return header_key_formatter_on_unknown_headers_.format(key);
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

absl::string_view PreserveCaseHeaderFormatter::getReasonPhrase() const {
  return absl::string_view(reason_phrase_);
};

class PreserveCaseFormatterFactory : public Envoy::Http::StatefulHeaderKeyFormatterFactory {
public:
  PreserveCaseFormatterFactory(const bool forward_reason_phrase,
                               const envoy::extensions::http::header_formatters::preserve_case::v3::
                                   PreserveCaseFormatterConfig::FormatterTypeOnUnknownHeaders
                                       formatter_type_on_unknown_headers)
      : forward_reason_phrase_(forward_reason_phrase),
        formatter_type_on_unknown_headers_(formatter_type_on_unknown_headers) {}

  // Envoy::Http::StatefulHeaderKeyFormatterFactory
  Envoy::Http::StatefulHeaderKeyFormatterPtr create() override {
    return std::make_unique<PreserveCaseHeaderFormatter>(forward_reason_phrase_,
                                                         formatter_type_on_unknown_headers_);
  }

private:
  const bool forward_reason_phrase_;
  const envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      FormatterTypeOnUnknownHeaders formatter_type_on_unknown_headers_;
};

class PreserveCaseFormatterFactoryConfig
    : public Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig {
public:
  // Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig
  std::string name() const override { return "preserve_case"; }

  Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr
  createFromProto(const Protobuf::Message& message) override {
    auto config =
        MessageUtil::downcastAndValidate<const envoy::extensions::http::header_formatters::
                                             preserve_case::v3::PreserveCaseFormatterConfig&>(
            message, ProtobufMessage::getStrictValidationVisitor());

    return std::make_shared<PreserveCaseFormatterFactory>(
        config.forward_reason_phrase(), config.formatter_type_on_unknown_headers());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::header_formatters::preserve_case::v3::
                                PreserveCaseFormatterConfig>();
  }
};

REGISTER_FACTORY(PreserveCaseFormatterFactoryConfig,
                 Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig);

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
