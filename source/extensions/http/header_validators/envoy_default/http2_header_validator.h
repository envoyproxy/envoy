#pragma once

#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class Http2HeaderValidator : public HeaderValidator {
public:
  Http2HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const HeaderValidatorConfigOverrides& config_overrides);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map);

private:
  /*
   * Validate the TE header.
   */
  HeaderValueValidationResult validateTEHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :authority pseudo header.
   */
  HeaderValueValidationResult validateAuthorityHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :protocol header.
   */
  HeaderValueValidationResult validateProtocolHeader(const ::Envoy::Http::HeaderString& value);

  HeaderEntryValidationResult
  validateGenericHeaderName(const ::Envoy::Http::HeaderString& name) override;

  HeaderEntryValidationResult validateRequestHeaderEntry(const ::Envoy::Http::HeaderString& key,
                                                         const ::Envoy::Http::HeaderString& value);

  HeaderEntryValidationResult validateResponseHeaderEntry(const ::Envoy::Http::HeaderString& key,
                                                          const ::Envoy::Http::HeaderString& value);

  // This method validites :path header value using character set that includes characters
  // prohibited by https://datatracker.ietf.org/doc/html/rfc3986#section-3.3 RFC.
  //
  // " < > [ ] ^ ` { } \ | SPACE TAB and all extended ASCII
  //
  // This method is called iff `envoy.uhv.allow_non_compliant_characters_in_path` is
  // `true`, which is the default value. Note the default will be switched to `false` in the future
  // for standard compliance.
  HeaderValidator::HeaderValueValidationResult
  validatePathHeaderWithAdditionalCharacters(const ::Envoy::Http::HeaderString& path_header_value);

  // Chooses path validation method based on the value of the override flag that affects the
  // validation algorithm.
  HeaderValidatorFunction getPathValidationMethod();

  const HeaderValidatorMap request_header_validator_map_;
};

class ServerHttp2HeaderValidator : public Http2HeaderValidator,
                                   public ::Envoy::Http::ServerHeaderValidator {
public:
  ServerHttp2HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const HeaderValidatorConfigOverrides& config_overrides)
      : Http2HeaderValidator(config, protocol, stats, config_overrides) {}

  ValidationResult
  validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map) override {
    return Http2HeaderValidator::validateRequestHeaders(header_map);
  }

  ValidationResult
  validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map) override {
    return Http2HeaderValidator::validateResponseHeaders(header_map);
  }

  ValidationResult
  validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map) override {
    return Http2HeaderValidator::validateRequestTrailers(trailer_map);
  }

  ValidationResult
  validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map) override {
    return Http2HeaderValidator::validateResponseTrailers(trailer_map);
  }

  RequestHeadersTransformationResult
  transformRequestHeaders(::Envoy::Http::RequestHeaderMap& header_map) override;

  TransformationResult
  transformRequestTrailers(::Envoy::Http::RequestTrailerMap& header_map) override;

  ResponseHeadersTransformationResult
  transformResponseHeaders(const ::Envoy::Http::ResponseHeaderMap&) override;
};

class ClientHttp2HeaderValidator : public Http2HeaderValidator,
                                   public ::Envoy::Http::ClientHeaderValidator {
public:
  ClientHttp2HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const HeaderValidatorConfigOverrides& config_overrides)
      : Http2HeaderValidator(config, protocol, stats, config_overrides) {}

  ValidationResult
  validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map) override {
    return Http2HeaderValidator::validateRequestHeaders(header_map);
  }

  ValidationResult
  validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map) override {
    return Http2HeaderValidator::validateResponseHeaders(header_map);
  }

  ValidationResult
  validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map) override {
    return Http2HeaderValidator::validateRequestTrailers(trailer_map);
  }

  ValidationResult
  validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map) override {
    return Http2HeaderValidator::validateResponseTrailers(trailer_map);
  }

  RequestHeadersTransformationResult
  transformRequestHeaders(const ::Envoy::Http::RequestHeaderMap&) override;

  TransformationResult transformResponseHeaders(::Envoy::Http::ResponseHeaderMap&) override;

private:
  std::string upgrade_type_;
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
