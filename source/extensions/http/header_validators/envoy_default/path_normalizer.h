#pragma once

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class PathNormalizer {
public:
  PathNormalizer(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config);

  using PathNormalizationResult = ::Envoy::Http::HeaderValidator::RequestHeaderMapValidationResult;

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
  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      config_;
};

using PathNormalizerPtr = std::unique_ptr<PathNormalizer>;

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
