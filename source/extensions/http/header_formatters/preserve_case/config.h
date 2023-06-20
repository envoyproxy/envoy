#pragma once

#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"
#include "envoy/http/header_formatter.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

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
  std::string name() const override {
    return "envoy.http.stateful_header_formatters.preserve_case";
  }

  Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr
  createFromProto(const Protobuf::Message& message) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::header_formatters::preserve_case::v3::
                                PreserveCaseFormatterConfig>();
  }
};

DECLARE_FACTORY(PreserveCaseFormatterFactoryConfig);

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
