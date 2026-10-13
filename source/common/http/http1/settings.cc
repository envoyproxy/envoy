#include "source/common/http/http1/settings.h"

#include "envoy/http/header_formatter.h"

#include "source/common/common/matchers.h"
#include "source/common/config/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {
namespace Http1 {

Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config,
                                 Server::Configuration::GenericFactoryContext& context,
                                 absl::Status& creation_status) {
  Http1Settings ret;
  ret.allow_absolute_url_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, allow_absolute_url, true);
  ret.accept_http_10_ = config.accept_http_10();
  ret.send_fully_qualified_url_ = config.send_fully_qualified_url();
  ret.default_host_for_http_10_ = config.default_host_for_http_10();
  ret.enable_trailers_ = config.enable_trailers();
  ret.allow_chunked_length_ = config.allow_chunked_length();

  if (!config.ignore_http_11_upgrade().empty()) {
    std::vector<Matchers::StringMatcherPtr> matchers;
    matchers.reserve(config.ignore_http_11_upgrade_size());
    for (const auto& matcher : config.ignore_http_11_upgrade()) {
      matchers.emplace_back(std::make_unique<Envoy::Matchers::StringMatcherImpl>(
          matcher, context.serverFactoryContext()));
    }
    ret.ignore_upgrade_matchers_ =
        std::make_shared<const std::vector<Matchers::StringMatcherPtr>>(std::move(matchers));
  }

  if (config.header_key_format().has_proper_case_words()) {
    ret.header_key_format_ = Http1Settings::HeaderKeyFormat::ProperCase;
  } else if (config.header_key_format().has_stateful_formatter()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig>(
            config.header_key_format().stateful_formatter());
    auto header_formatter_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
        config.header_key_format().stateful_formatter().typed_config(),
        context.messageValidationVisitor(), factory);
    auto formatter_factory_or_error =
        factory.createFactoryFromProto(*header_formatter_config, context);
    if (!formatter_factory_or_error.ok()) {
      // Return default settings rather than the half-populated ones, so a caller that stores the
      // result before checking the status cannot end up using a partially configured protocol.
      creation_status = formatter_factory_or_error.status();
      return {};
    }
    // The factory should never return a nullptr when the creation is successful.
    ASSERT(formatter_factory_or_error.value() != nullptr);
    ret.header_key_format_ = Http1Settings::HeaderKeyFormat::StatefulFormatter;
    ret.stateful_header_key_formatter_ = std::move(formatter_factory_or_error.value());
  }

  ret.allow_custom_methods_ = config.allow_custom_methods();

  return ret;
}

Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config,
                                 Server::Configuration::GenericFactoryContext& context,
                                 const Protobuf::BoolValue& hcm_stream_error, bool validate_scheme,
                                 absl::Status& creation_status) {
  Http1Settings ret = parseHttp1Settings(config, context, creation_status);
  if (!creation_status.ok()) {
    return {};
  }
  ret.validate_scheme_ = validate_scheme;

  if (config.has_override_stream_error_on_invalid_http_message()) {
    // override_stream_error_on_invalid_http_message, if set, takes precedence over any HCM
    // stream_error_on_invalid_http_message
    ret.stream_error_on_invalid_http_message_ =
        config.override_stream_error_on_invalid_http_message().value();
  } else {
    // fallback to HCM value
    ret.stream_error_on_invalid_http_message_ = hcm_stream_error.value();
  }

  return ret;
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
