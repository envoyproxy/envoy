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

  using HeaderValueValidationResult = RejectResult;
  /*
   * Validate the :method pseudo header, honoring the restrict_http_methods configuration option.
   */
  virtual HeaderValueValidationResult
  validateMethodHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :status response pseudo header based on the range of valid response statuses.
   */
  virtual HeaderValueValidationResult
  validateStatusHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate any request or response header name.
   */
  virtual HeaderEntryValidationResult
  validateGenericHeaderName(const ::Envoy::Http::HeaderString& name);

  /*
   * Validate any request or response header value.
   */
  virtual HeaderValueValidationResult
  validateGenericHeaderValue(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the Content-Length request and response header as a whole number integer. The RFC
   * states that multiple Content-Length values are acceptable if they are all the same value.
   * However, UHV does not allow multiple values currently because the comma character will be
   * rejected. We can add an option to allow multiple Content-Length values in the future if
   * needed.
   */
  virtual HeaderValueValidationResult
  validateContentLengthHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :scheme pseudo header.
   */
  virtual HeaderValueValidationResult
  validateSchemeHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the Host header or :authority pseudo header. This method does not allow the
   * userinfo component (user:pass@host).
   */
  virtual HeaderValueValidationResult validateHostHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :path pseudo header. This method only validates that the :path header only
   * contains valid characters and does not validate the syntax or form of the path URI.
   */
  virtual HeaderValueValidationResult
  validatePathHeaderCharacters(const ::Envoy::Http::HeaderString& value);

  /*
   * Check if the Transfer-Encoding header contains the "chunked" transfer encoding.
   */
  bool hasChunkedTransferEncoding(const ::Envoy::Http::HeaderString& value);

protected:
  /*
   * An internal class that stores the result of validating syntax-specific URI hosts.
   */
  class HostHeaderValidationResult {
  public:
    HostHeaderValidationResult(RejectAction action, absl::string_view details,
                               absl::string_view address, absl::string_view port)
        : result_(action, details, address, port) {
      ENVOY_BUG(action == RejectAction::Accept || !details.empty(),
                "Error details must not be empty in case of an error");
    }

    static HostHeaderValidationResult reject(absl::string_view details) {
      return {RejectAction::Reject, details, "", ""};
    }

    static HostHeaderValidationResult success(absl::string_view address, absl::string_view port) {
      return {RejectAction::Accept, "", address, port};
    }

    bool ok() const { return action() == RejectAction::Accept; }

    RejectAction action() const { return std::get<0>(result_); }

    absl::string_view details() const { return std::get<1>(result_); }

    // The address component of the URI path.
    absl::string_view address() const { return std::get<2>(result_); }

    // The port component of the URI path, including the leading ":" delimiter.
    absl::string_view portAndDelimiter() const { return std::get<3>(result_); }

  private:
    std::tuple<RejectAction, std::string, absl::string_view, absl::string_view> result_;
  };

  /*
   * Validate an IPv6 host header value. The port specifier, if included in the host string, is
   * stored in the return details on success.
   */
  HostHeaderValidationResult validateHostHeaderIPv6(absl::string_view host);

  /*
   * Validate a reg-name host header value. The port specifier, if included in the host string, is
   * stored in the return details on success.
   */
  HostHeaderValidationResult validateHostHeaderRegName(absl::string_view host);

  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      config_;
  ::Envoy::Http::Protocol protocol_;
  StreamInfo::StreamInfo& stream_info_;
  const ::Envoy::Http::HeaderValues& header_values_;
};

struct UhvResponseCodeDetailValues {
  const std::string InvalidNameCharacters = "uhv.invalid_name_characters";
  const std::string InvalidValueCharacters = "uhv.invalid_value_characters";
  const std::string InvalidUrl = "uhv.invalid_url";
  const std::string InvalidHost = "uhv.invalid_host";
  const std::string InvalidScheme = "uhv.invalid_scheme";
  const std::string InvalidMethod = "uhv.invalid_method";
  const std::string InvalidContentLength = "uhv.invalid_content_length";
  const std::string InvalidUnderscore = "uhv.unexpected_underscore";
  const std::string InvalidStatus = "uhv.invalid_status";
  const std::string EmptyHeaderName = "uhv.empty_header_name";
  const std::string InvalidPseudoHeader = "uhv.invalid_pseudo_header";
  const std::string InvalidHostDeprecatedUserInfo = "uhv.invalid_host_deprecated_user_info";
};

using UhvResponseCodeDetail = ConstSingleton<UhvResponseCodeDetailValues>;

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
