#pragma once

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator.h"

#include "source/extensions/http/header_validators/envoy_default/config_overrides.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class PathNormalizer {
public:
  PathNormalizer(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      const ConfigOverrides& config_overrides);

  using PathNormalizationResult = ::Envoy::Http::HeaderValidator::RejectOrRedirectResult;

  /*
   * Normalize the path component of the :path header and update the header value. This method does
   * not perform any validation of the normalized :path such as validating the character set.
   */
  PathNormalizationResult normalizePathUri(::Envoy::Http::RequestHeaderMap& header_map) const;

  /*
   * The result of attempting to normalize and decode a percent-encoded octet.
   */
  enum class PercentDecodeResult {
    // The percent encoding is invalid and could not be decoded.
    Invalid,
    // The percent encoding is valid but decodes to an unallowed character.
    Reject,
    // The percent encoding is valid and was normalized to UPPERCASE.
    Normalized,
    // The percent encoding is valid and was decoded.
    Decoded,
    // The percent ending is valid, was decoded, and, based on the active configuration, the
    // response should redirect to the normalized path.
    DecodedRedirect
  };

  /*
   * A decoded octet consisting of the decode result and the decoded character.
   */
  class DecodedOctet {
  public:
    DecodedOctet(PercentDecodeResult result, char octet = '\0') : result_(result), octet_(octet) {}

    PercentDecodeResult result() const { return result_; }
    char octet() const { return octet_; }

  private:
    PercentDecodeResult result_;
    char octet_;
  };

  /*
   * Normalize a percent encoded octet (%XX) to uppercase and attempt to decode to a character. The
   * octet argument must start with the "%" character and is normalized in-place to UPPERCASE.
   */
  DecodedOctet normalizeAndDecodeOctet(std::string::iterator iter, std::string::iterator end) const;

private:
  /*
   * Normalization pass: normalize percent-encoded octets to UPPERCASE and decode valid octets.
   */
  PathNormalizationResult decodePass(std::string& path) const;
  /*
   * Normalization pass: merge duplicate slashes.
   */
  PathNormalizationResult mergeSlashesPass(std::string& path) const;
  /*
   * Normalization pass: collapse dot and dot-dot segments.
   */
  PathNormalizationResult collapseDotSegmentsPass(std::string& path) const;
  /*
   * Split the path and query parameters / fragment components. The return value is a 2-item tuple:
   * (path, query_params).
   */
  std::tuple<absl::string_view, absl::string_view>
  splitPathAndQueryParams(absl::string_view path_and_query_params) const;
  /**
   * Translate backslash to forward slash. Enabled by the
   * envoy.reloadable_features.uhv_translate_backslash_to_slash flag.
   */
  void translateBackToForwardSlashes(std::string& path) const;

  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      config_;
  const ConfigOverrides config_overrides_;
};

using PathNormalizerPtr = std::unique_ptr<PathNormalizer>;

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
