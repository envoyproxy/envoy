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
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats);

  using ValidationResult = ::Envoy::Http::HeaderValidatorBase::ValidationResult;

  ValidationResult validateRequestHeaders(const ::Envoy::Http::RequestHeaderMap& header_map);

  ValidationResult validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map);

  ValidationResult validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map);

  ValidationResult validateResponseTrailers(const ::Envoy::Http::ResponseTrailerMap& trailer_map);

protected:
  /**
   * Checks for presence of both Transfer-Encoding and Content-Length headers and
   * removes the Content-Length header iff the http1_protocol_options.allow_chunked_length
   * config options is true.
   * If the http1_protocol_options.allow_chunked_length is false a request with both
   * Transfer-Encoding and Content-Length headers is rejected in the validateRequestHeaders method.
   * Additionally if request is CONNECT and Content-Length is 0, the Content-Length header is
   * removed.
   */
  void sanitizeContentLength(::Envoy::Http::RequestHeaderMap& header_map);

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

  const HeaderValidatorMap request_header_validator_map_;
};

class ServerHttp1HeaderValidator : public Http1HeaderValidator,
                                   public ::Envoy::Http::HeaderValidator {
public:
  ServerHttp1HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats)
      : Http1HeaderValidator(config, protocol, stats) {}

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
};

class ClientHttp1HeaderValidator : public Http1HeaderValidator,
                                   public ::Envoy::Http::ClientHeaderValidator {
public:
  ClientHttp1HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats)
      : Http1HeaderValidator(config, protocol, stats) {}

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

  TransformationResult transformResponseHeaders(::Envoy::Http::ResponseHeaderMap&) override {
    return TransformationResult::success();
  }
};

using ServerHttp1HeaderValidatorPtr = std::unique_ptr<ServerHttp1HeaderValidator>;
using ClientHttp1HeaderValidatorPtr = std::unique_ptr<ClientHttp1HeaderValidator>;

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
