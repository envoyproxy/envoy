#pragma once

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator.h"

#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

/*
 * Base class for all HTTP codec header validations. This class has several methods to validate
 * headers that are shared across multiple codec versions where the RFC guidance did not change.
 */
class HeaderValidator : public ::Envoy::Http::HeaderValidator {
public:
  HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, StreamInfo::StreamInfo& stream_info);

  /*
   * Validate the :method pseudo header, honoring the restrict_http_methods configuration option.
   */
  virtual HeaderEntryValidationResult
  validateMethodHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Configuration for validateStatusHeader method.
   */
  enum class StatusPseudoHeaderValidationMode {
    // Only accept whole number integer values.
    WholeNumber,

    // Only accept values in the following range: 100 <= status <= 599.
    ValueRange,

    // Only accept RFC registered status codes:
    // https://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml.
    OfficialStatusCodes,
  };

  /*
   * Validate the :status response pseudo header.
   */
  virtual HeaderEntryValidationResult
  validateStatusHeader(const StatusPseudoHeaderValidationMode& mode,
                       const ::Envoy::Http::HeaderString& value);

  /*
   * Validate any request or response header name.
   */
  virtual HeaderEntryValidationResult
  validateGenericHeaderName(const ::Envoy::Http::HeaderString& name);

  /*
   * Validate any request or response header value.
   */
  virtual HeaderEntryValidationResult
  validateGenericHeaderValue(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the Content-Length request and response header as a whole number integer.
   */
  virtual HeaderEntryValidationResult
  validateContentLengthHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :scheme pseudo header.
   */
  virtual HeaderEntryValidationResult
  validateSchemeHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the Host header or :authority pseudo header. This method does not allow the
   * userinfo component (user:pass@host).
   */
  virtual HeaderEntryValidationResult validateHostHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :path pseudo header. This method only validates that the :path header only
   * contains valid characters and does not validate the syntax or form of the path URI.
   */
  virtual HeaderEntryValidationResult
  validateGenericPathHeader(const ::Envoy::Http::HeaderString& value);

protected:
  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      config_;
  ::Envoy::Http::Protocol protocol_;
  StreamInfo::StreamInfo& stream_info_;
  const ::Envoy::Http::HeaderValues& header_values_;
};

struct UhvResponseCodeDetailValues {
  const absl::string_view InvalidCharacters = "uhv.invalid_characters";
  const absl::string_view InvalidUrl = "uhv.invalid_url";
  const absl::string_view InvalidHost = "uhv.invalid_host";
  const absl::string_view InvalidScheme = "uhv.invalid_scheme";
  const absl::string_view InvalidMethod = "uhv.invalid_method";
  const absl::string_view InvalidContentLength = "uhv.invalid_content_length";
  const absl::string_view InvalidUnderscore = "uhv.unexpected_underscore";
  const absl::string_view InvalidStatus = "uhv.invalid_status";
  const absl::string_view EmptyHeaderName = "uhv.empty_header_name";
  const absl::string_view InvalidPseudoHeader = "uhv.invalid_pseudo_header";
};

using UhvResponseCodeDetail = ConstSingleton<UhvResponseCodeDetailValues>;

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
