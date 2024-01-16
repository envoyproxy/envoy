#pragma once

#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class Http1HeaderValidator : public HeaderValidator {
public:
  Http1HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const ConfigOverrides& config_overrides);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map);

  ::Envoy::Http::HeaderValidator::ValidationResult
  validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map);

protected:
  /**
   * Checks for presence of both Transfer-Encoding and Content-Length headers and
   * removes the Content-Length header iff the http1_protocol_options.allow_chunked_length
   * config options is true.
   * If the http1_protocol_options.allow_chunked_length is false a request with both
   * Transfer-Encoding and Content-Length headers is rejected in the validateRequestHeaders method.
   */
  void sanitizeContentLength(::Envoy::Http::RequestOrResponseHeaderMap& header_map);

  /**
   * Validate Transfer-Encoding and Content-Length headers.
   * HTTP/1.1 disallows a Transfer-Encoding and Content-Length headers,
   * https://www.rfc-editor.org/rfc/rfc9112.html#section-6.2:
   *
   * A sender MUST NOT send a Content-Length header field in any message that
   * contains a Transfer-Encoding header field.
   *
   * The http1_protocol_options.allow_chunked_length config setting can
   * override the RFC compliance to allow a Transfer-Encoding of "chunked" with
   * a Content-Length set. In this exception case, we remove the Content-Length
   * header in the transform[Request/Response]Headers() method.
   */
  ::Envoy::Http::HeaderValidator::ValidationResult validateContentLengthAndTransferEncoding(
      const ::Envoy::Http::RequestOrResponseHeaderMap& header_map);

private:
  /*
   * Validate the Transfer-Encoding header.
   */
  HeaderValueValidationResult
  validateTransferEncodingHeader(const ::Envoy::Http::HeaderString& value) const;

  HeaderEntryValidationResult validateRequestHeaderEntry(const ::Envoy::Http::HeaderString& key,
                                                         const ::Envoy::Http::HeaderString& value);

  HeaderEntryValidationResult validateResponseHeaderEntry(const ::Envoy::Http::HeaderString& key,
                                                          const ::Envoy::Http::HeaderString& value);

  // This method validates :path header value using character set that includes characters
  // that https://datatracker.ietf.org/doc/html/rfc3986#section-3.3 RFC is ambiguous about:
  //
  // " < > [ ] ^ ` { } \ | #
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

class ServerHttp1HeaderValidator : public Http1HeaderValidator,
                                   public ::Envoy::Http::ServerHeaderValidator {
public:
  ServerHttp1HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const ConfigOverrides& config_overrides)
      : Http1HeaderValidator(config, protocol, stats, config_overrides) {}

  ValidationResult
  validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map) override {
    return Http1HeaderValidator::validateRequestHeaders(header_map);
  }

  ValidationResult
  validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map) override {
    return Http1HeaderValidator::validateResponseHeaders(header_map);
  }

  ValidationResult
  validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map) override {
    return Http1HeaderValidator::validateRequestTrailers(trailer_map);
  }

  ValidationResult
  validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map) override {
    return Http1HeaderValidator::validateResponseTrailers(trailer_map);
  }

  RequestHeadersTransformationResult
  transformRequestHeaders(::Envoy::Http::RequestHeaderMap& header_map) override;

  TransformationResult
  transformRequestTrailers(::Envoy::Http::RequestTrailerMap& header_map) override;

  ResponseHeadersTransformationResult
  transformResponseHeaders(const ::Envoy::Http::ResponseHeaderMap&) override {
    return ResponseHeadersTransformationResult::success();
  }

private:
  /**
   * If request is CONNECT and Content-Length is 0, the Content-Length header is removed.
   * Otherwise calls Http1HeaderValidator::sanitizeContentLength()
   */
  void sanitizeContentLength(::Envoy::Http::RequestHeaderMap& header_map);
};

class ClientHttp1HeaderValidator : public Http1HeaderValidator,
                                   public ::Envoy::Http::ClientHeaderValidator {
public:
  ClientHttp1HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const ConfigOverrides& config_overrides)
      : Http1HeaderValidator(config, protocol, stats, config_overrides) {}

  ValidationResult
  validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map) override {
    return Http1HeaderValidator::validateRequestHeaders(header_map);
  }

  ValidationResult
  validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map) override {
    return Http1HeaderValidator::validateResponseHeaders(header_map);
  }

  ValidationResult
  validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map) override {
    return Http1HeaderValidator::validateRequestTrailers(trailer_map);
  }

  ValidationResult
  validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map) override {
    return Http1HeaderValidator::validateResponseTrailers(trailer_map);
  }

  RequestHeadersTransformationResult
  transformRequestHeaders(const ::Envoy::Http::RequestHeaderMap&) override {
    return RequestHeadersTransformationResult::success();
  }

  TransformationResult transformResponseHeaders(::Envoy::Http::ResponseHeaderMap&) override;
};

using ServerHttp1HeaderValidatorPtr = std::unique_ptr<ServerHttp1HeaderValidator>;
using ClientHttp1HeaderValidatorPtr = std::unique_ptr<ClientHttp1HeaderValidator>;

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
