#pragma once

#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

struct Http2HeaderValidatorConfig {
  // Relax validation of the :path header by allowing extended ASCII characters for HTTP/2 protocol
  // only. This option also causes the extended ASCII to be URL encoded during path normalization,
  // if it is enabled.
  //
  // This option currently is `true` by default and can be overridden using the
  // "envoy.uhv.allow_extended_ascii_in_path_for_http2" runtime value. Note that the default value
  // will be changed to `false` in the future to make Envoy behavior standard compliant and
  // consistent across all HTTP protocol versions.
  //
  // This option provides backward compatibility with the existing (pre header validator) Envoy
  // behavior. HTTP/2 codec was not compliant with the
  // https://datatracker.ietf.org/doc/html/rfc3986#section-3.3 by allowing extended ASCII in the
  // :path header. Envoy would later URL encode extended ASCII in URI path portion (but not in query
  // or fragment portions) during path normalization (this is the behavior of the Chromium library
  // Envoy uses for URL path normalization).
  //
  // With header validator enabled requests with extended ASCII in the :path header are rejected.
  const bool allow_extended_ascii_in_path_{true};
};

class Http2HeaderValidator : public HeaderValidator {
public:
  Http2HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const Http2HeaderValidatorConfig& http2_config);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map);

protected:
  const Http2HeaderValidatorConfig http2_config_;

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

  // Relax validation of character set in the :path header by allowing extended ASCII
  // characters. This method is called iff `envoy.uhv.allow_extended_ascii_in_path_for_http2` is
  // `true`, which is the default value. Note the default will be switched to `false` in the future
  // for standard compliance.
  HeaderValidator::HeaderValueValidationResult
  validatePathHeaderCharactersExtendedAsciiAllowed(const ::Envoy::Http::HeaderString& value);

  // Chooses path validation method based on the value of runtime flags that affect the validation
  // algorithm.
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
      const Http2HeaderValidatorConfig& http2_config)
      : Http2HeaderValidator(config, protocol, stats, http2_config) {}

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

private:
  // URL-encode extended ASCII characters in URL path. This method is called iff
  // `envoy.uhv.allow_extended_ascii_in_path_for_http2` is true.
  // This method is provided for backward compatibility with Envoy's pre header validator
  // behavior. See comments in the Http2HeaderValidatorConfig declaration for more
  // information.
  void encodeExtendedAsciiInPath(::Envoy::Http::RequestHeaderMap& header_map);
};

class ClientHttp2HeaderValidator : public Http2HeaderValidator,
                                   public ::Envoy::Http::ClientHeaderValidator {
public:
  ClientHttp2HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats)
      : Http2HeaderValidator(config, protocol, stats, Http2HeaderValidatorConfig{false}) {}

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
