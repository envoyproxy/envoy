#pragma once

#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.validate.h"
#include "envoy/http/header_formatter.h"

#include "source/common/http/http1/header_formatter.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

class PreserveCaseHeaderFormatter : public Envoy::Http::StatefulHeaderKeyFormatter {
public:
  // Envoy::Http::StatefulHeaderKeyFormatter
  PreserveCaseHeaderFormatter(
      const bool forward_reason_phrase,
      const envoy::extensions::http::header_formatters::preserve_case::v3::
          PreserveCaseFormatterConfig::FormatterTypeOnEnvoyHeaders formatter_type_on_envoy_headers);

  std::string format(absl::string_view key) const override;
  void processKey(absl::string_view key) override;
  void setReasonPhrase(absl::string_view reason_phrase) override;
  absl::string_view getReasonPhrase() const override;
  Envoy::Http::HeaderKeyFormatterOptConstRef formatterOnEnvoyHeaders() const;

private:
  StringUtil::CaseUnorderedSet original_header_keys_;
  bool forward_reason_phrase_{false};
  std::string reason_phrase_;
  envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      FormatterTypeOnEnvoyHeaders formatter_type_on_envoy_headers_{
          envoy::extensions::http::header_formatters::preserve_case::v3::
              PreserveCaseFormatterConfig::DEFAULT};
  Envoy::Http::HeaderKeyFormatterConstPtr header_key_formatter_on_enovy_headers_;
};

class PreserveCaseFormatterFactory : public Envoy::Http::StatefulHeaderKeyFormatterFactory {
public:
  PreserveCaseFormatterFactory(
      const bool forward_reason_phrase,
      const envoy::extensions::http::header_formatters::preserve_case::v3::
          PreserveCaseFormatterConfig::FormatterTypeOnEnvoyHeaders formatter_type_on_envoy_headers)
      : forward_reason_phrase_(forward_reason_phrase),
        formatter_type_on_envoy_headers_(formatter_type_on_envoy_headers) {}

  // Envoy::Http::StatefulHeaderKeyFormatterFactory
  Envoy::Http::StatefulHeaderKeyFormatterPtr create() override {
    return std::make_unique<PreserveCaseHeaderFormatter>(forward_reason_phrase_,
                                                         formatter_type_on_envoy_headers_);
  }

private:
  const bool forward_reason_phrase_;
  const envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      FormatterTypeOnEnvoyHeaders formatter_type_on_envoy_headers_;
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

    return std::make_shared<PreserveCaseFormatterFactory>(config.forward_reason_phrase(),
                                                          config.formatter_type_on_envoy_headers());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::header_formatters::preserve_case::v3::
                                PreserveCaseFormatterConfig>();
  }
};

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
