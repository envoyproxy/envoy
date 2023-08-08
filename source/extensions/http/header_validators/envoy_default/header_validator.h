#pragma once

#include <functional>

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator.h"

#include "source/common/http/headers.h"
#include "source/extensions/http/header_validators/envoy_default/config_overrides.h"
#include "source/extensions/http/header_validators/envoy_default/path_normalizer.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

/*
 * Base class for all HTTP codec header validations. This class has several methods to validate
 * headers that are shared across multiple codec versions where the RFC guidance did not change.
 */
class HeaderValidator {
public:
  HeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      ::Envoy::Http::Protocol protocol, ::Envoy::Http::HeaderValidatorStats& stats,
      const ConfigOverrides& config_overrides);
  virtual ~HeaderValidator() = default;

  using HeaderEntryValidationResult = ::Envoy::Http::HeaderValidator::RejectResult;
  using HeaderValueValidationResult = ::Envoy::Http::HeaderValidator::RejectResult;
  /*
   * Validate the :method pseudo header, honoring the restrict_http_methods configuration option.
   */
  HeaderValueValidationResult validateMethodHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :status response pseudo header based on the range of valid response statuses.
   */
  HeaderValueValidationResult validateStatusHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate any request or response header name.
   */
  virtual HeaderEntryValidationResult
  validateGenericHeaderName(const ::Envoy::Http::HeaderString& name);

  /*
   * Validate any request or response header value.
   */
  HeaderValueValidationResult validateGenericHeaderValue(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the Content-Length request and response header as a whole number integer. The RFC
   * states that multiple Content-Length values are acceptable if they are all the same value.
   * However, UHV does not allow multiple values currently because the comma character will be
   * rejected. We can add an option to allow multiple Content-Length values in the future if
   * needed.
   */
  HeaderValueValidationResult validateContentLengthHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :scheme pseudo header.
   */
  HeaderValueValidationResult validateSchemeHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the Host header or :authority pseudo header. This method does not allow the
   * userinfo component (user:pass@host).
   */
  HeaderValueValidationResult validateHostHeader(const ::Envoy::Http::HeaderString& value);

  /*
   * Validate the :path pseudo header. This method only validates that the :path header only
   * contains valid characters and does not validate the syntax or form of the path URI.
   */
  HeaderValueValidationResult
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
    using RejectAction = ::Envoy::Http::HeaderValidator::RejectAction;
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

  /*
   * Validate a header value. The `protocol_specific_header_validators` map contains validation
   * function for protocol specific header keys. If the header key is not found in the
   * `protocol_specific_header_validators` the header key is checked by calling the
   * `validateGenericHeaderName` method (Note that `validateGenericHeaderName` is virtual and has
   * different behavior for H/1 and H/2, H/3 validators) and the header value is checked by calling
   * the `validateGenericHeaderValue` method.
   */
  using HeaderValidatorFunction = std::function<HeaderValidator::HeaderValueValidationResult(
      const ::Envoy::Http::HeaderString&)>;
  using HeaderValidatorMap = absl::node_hash_map<absl::string_view, HeaderValidatorFunction>;
  HeaderEntryValidationResult
  validateGenericRequestHeaderEntry(const ::Envoy::Http::HeaderString& key,
                                    const ::Envoy::Http::HeaderString& value,
                                    const HeaderValidatorMap& protocol_specific_header_validators);

  /*
   * Common method for validating request or response trailers.
   */
  ::Envoy::Http::HeaderValidator::ValidationResult
  validateTrailers(const ::Envoy::Http::HeaderMap& trailers);

  /**
   * Removes headers with underscores in their names iff the headers_with_underscores_action
   * config value is DROP. Noop otherwise.
   * The REJECT config option for header names with underscores is handled in the
   * validateRequestHeaders or validateRequestTrailers methods.
   */
  void sanitizeHeadersWithUnderscores(::Envoy::Http::HeaderMap& header_map);

  /*
   * Validate the :path pseudo header using specific allowed character set.
   */
  HeaderValueValidationResult
  validatePathHeaderCharacterSet(const ::Envoy::Http::HeaderString& value,
                                 const std::array<uint32_t, 8>& allowed_path_chracters,
                                 const std::array<uint32_t, 8>& allowed_query_fragment_characters);

  // URL-encode additional characters in URL path. This method is called iff
  // `envoy.uhv.allow_non_compliant_characters_in_path` is true.
  // Encoded characters:
  //
  // " < > ^ ` { } | TAB space extended-ASCII
  // This method is provided for backward compatibility with Envoy's pre header validator
  // behavior. See comments in the HeaderValidatorConfigOverrides declaration above for more
  // information.
  void encodeAdditionalCharactersInPath(::Envoy::Http::RequestHeaderMap& header_map);
  /**
   * Check if the :path header contains a fragment. If the fragment is found it is stripped from
   * the :path.
   */
  void sanitizePathWithFragment(::Envoy::Http::RequestHeaderMap& header_map);

  /**
   * Decode percent-encoded slash characters based on configuration.
   */
  PathNormalizer::PathNormalizationResult
  sanitizeEncodedSlashes(::Envoy::Http::RequestHeaderMap& header_map);

  /**
   * Transform URL path according to configuration (i.e. apply path normalization).
   */
  PathNormalizer::PathNormalizationResult
  transformUrlPath(::Envoy::Http::RequestHeaderMap& header_map);

  /**
   * Check for presence of %00 sequence based on configuration.
   * Reject request if %00 sequence was found.
   */
  HeaderValueValidationResult
  checkForPercent00InUrlPath(const ::Envoy::Http::RequestHeaderMap& header_map);

  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      config_;
  ::Envoy::Http::Protocol protocol_;
  const ConfigOverrides config_overrides_;
  const ::Envoy::Http::HeaderValues& header_values_;
  ::Envoy::Http::HeaderValidatorStats& stats_;
  const PathNormalizer path_normalizer_;
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
