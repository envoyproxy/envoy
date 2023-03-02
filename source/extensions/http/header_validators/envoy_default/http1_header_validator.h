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

  HeaderEntryValidationResult validateRequestHeaderEntry(const ::Envoy::Http::HeaderString& key,
                                                         const ::Envoy::Http::HeaderString& value);

  HeaderEntryValidationResult validateResponseHeaderEntry(const ::Envoy::Http::HeaderString& key,
                                                          const ::Envoy::Http::HeaderString& value);

  RequestHeaderMapValidationResult
  validateRequestHeaderMap(::Envoy::Http::RequestHeaderMap& header_map);

  ResponseHeaderMapValidationResult
  validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap& header_map);

  TrailerValidationResult validateRequestTrailerMap(::Envoy::Http::RequestTrailerMap& trailer_map);

  TrailerValidationResult
  validateResponseTrailerMap(::Envoy::Http::ResponseTrailerMap& trailer_map);

  /*
   * Validate the Transfer-Encoding header.
   */
  HeaderValueValidationResult
  validateTransferEncodingHeader(const ::Envoy::Http::HeaderString& value) const;

private:
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

  HeaderEntryValidationResult
  validateRequestHeaderEntry(const ::Envoy::Http::HeaderString& key,
                             const ::Envoy::Http::HeaderString& value) override {
    return Http1HeaderValidator::validateRequestHeaderEntry(key, value);
  }

  HeaderEntryValidationResult
  validateResponseHeaderEntry(const ::Envoy::Http::HeaderString& key,
                              const ::Envoy::Http::HeaderString& value) override {
    return Http1HeaderValidator::validateResponseHeaderEntry(key, value);
  }

  RequestHeaderMapValidationResult
  validateRequestHeaderMap(::Envoy::Http::RequestHeaderMap& header_map) override {
    return Http1HeaderValidator::validateRequestHeaderMap(header_map);
  }

  ConstResponseHeaderMapValidationResult
  validateResponseHeaderMap(const ::Envoy::Http::ResponseHeaderMap&) override {
    return {RejectResult::success(), nullptr};
  }

  TrailerValidationResult
  validateRequestTrailerMap(::Envoy::Http::RequestTrailerMap& trailer_map) override {
    return Http1HeaderValidator::validateRequestTrailerMap(trailer_map);
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

  HeaderEntryValidationResult
  validateRequestHeaderEntry(const ::Envoy::Http::HeaderString& key,
                             const ::Envoy::Http::HeaderString& value) override {
    return Http1HeaderValidator::validateRequestHeaderEntry(key, value);
  }

  HeaderEntryValidationResult
  validateResponseHeaderEntry(const ::Envoy::Http::HeaderString& key,
                              const ::Envoy::Http::HeaderString& value) override {
    return Http1HeaderValidator::validateResponseHeaderEntry(key, value);
  }

  ConstRequestHeaderMapValidationResult
  validateRequestHeaderMap(const ::Envoy::Http::RequestHeaderMap&) override {
    // TODO(yanavlasov): Validate request header map before sending it upstream
    return {RejectResult::success(), nullptr};
  }

  ResponseHeaderMapValidationResult
  validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap& header_map) override {
    return Http1HeaderValidator::validateResponseHeaderMap(header_map);
  }

  TrailerValidationResult
  validateResponseTrailerMap(::Envoy::Http::ResponseTrailerMap& trailer_map) override {
    return Http1HeaderValidator::validateResponseTrailerMap(trailer_map);
  }
};

using ServerHttp1HeaderValidatorPtr = std::unique_ptr<ServerHttp1HeaderValidator>;
using ClientHttp1HeaderValidatorPtr = std::unique_ptr<ClientHttp1HeaderValidator>;

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
