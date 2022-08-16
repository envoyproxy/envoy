#include "source/extensions/http/header_validators/envoy_default/path_normalizer.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::envoy::extensions::http::header_validators::envoy_default::v3::
    HeaderValidatorConfig_UriPathNormalizationOptions;
using ::Envoy::Http::RequestHeaderMap;

struct PathNormalizerResponseCodeDetailValues {
  const std::string RedirectNormalized = "uhv.path_noramlization_redirect";
};

using PathNormalizerResponseCodeDetail = ConstSingleton<PathNormalizerResponseCodeDetailValues>;

PathNormalizer::PathNormalizer(const HeaderValidatorConfig& config) : config_(config) {}

PathNormalizer::DecodedOctet PathNormalizer::normalizeAndDecodeOctet(char* octet) const {
  //
  // From RFC 3986: https://datatracker.ietf.org/doc/html/rfc3986#section-2.1
  //
  // SPELLCHECKER(off)
  // pct-encoded = "%" HEXDIG HEXDIG
  //
  // The uppercase hexadecimal digits 'A' through 'F' are equivalent to
  // the lowercase digits 'a' through 'f', respectively. If two URIs
  // differ only in the case of hexadecimal digits used in percent-encoded
  // octets, they are equivalent. For consistency, URI producers and
  // normalizers should use uppercase hexadecimal digits for all percent-
  // encodings.
  //
  // Also from RFC 3986: https://datatracker.ietf.org/doc/html/rfc3986#section-2.4
  //
  // When a URI is dereferenced, the components and subcomponents significant
  // to the scheme-specific dereferencing process (if any) must be parsed and
  // separated before the percent-encoded octets within those components can
  // be safely decoded, as otherwise the data may be mistaken for component
  // delimiters. The only exception is for percent-encoded octets corresponding
  // to characters in the unreserved set, which can be decoded at any time.
  // SPELLCHECKER(on)
  //
  char ch;

  if (!isxdigit(octet[1]) || !isxdigit(octet[2])) {
    return {PercentDecodeResult::Invalid};
  }

  // normalize to UPPERCASE
  octet[1] = octet[1] >= 'a' && octet[1] <= 'z' ? octet[1] ^ 0x20 : octet[1];
  octet[2] = octet[2] >= 'a' && octet[2] <= 'z' ? octet[2] ^ 0x20 : octet[2];

  // decode to character
  ch = octet[1] >= 'A' ? (octet[1] - 'A' + 10) : (octet[1] - '0');
  ch *= 16;
  ch += octet[2] >= 'A' ? (octet[2] - 'A' + 10) : (octet[2] - '0');

  if (testChar(kUnreservedCharTable, ch)) {
    // Based on RFC, only decode characters in the UNRESERVED set.
    return {PercentDecodeResult::Decoded, ch};
  }

  if (ch == '/' || ch == '\\') {
    // We decoded a slash character and how we handle it depends on the active configuration.
    switch (config_.uri_path_normalization_options().path_with_escaped_slashes_action()) {
    case HeaderValidatorConfig_UriPathNormalizationOptions::IMPLEMENTATION_SPECIFIC_DEFAULT:
      ABSL_FALLTHROUGH_INTENDED;
    case HeaderValidatorConfig_UriPathNormalizationOptions::KEEP_UNCHANGED:
      // default implementation: normalize the encoded octet and accept the path
      return {PercentDecodeResult::Normalized};

    case HeaderValidatorConfig_UriPathNormalizationOptions::REJECT_REQUEST:
      // Reject the entire request
      return {PercentDecodeResult::Reject};

    case HeaderValidatorConfig_UriPathNormalizationOptions::UNESCAPE_AND_FORWARD:
      // Decode the slash and accept the path.
      return {PercentDecodeResult::Decoded, ch};

    case HeaderValidatorConfig_UriPathNormalizationOptions::UNESCAPE_AND_REDIRECT:
      // Decode the slash and response with a redirect to the normalized path.
      return {PercentDecodeResult::DecodedRedirect, ch};

    default:
      // This should never occur but it's here to make the compiler happy because of the extra
      // values added by protobuf.
      break;
    }
  }

  // The octet is a valid encoding but it wasn't be decoded because it was outside the UNRESERVED
  // character set.
  return {PercentDecodeResult::Normalized};
}

HeaderValidator::RequestHeaderMapValidationResult
PathNormalizer::normalizePathUri(RequestHeaderMap& header_map) const {
  // Make a copy of the original path so we can edit it in place.
  absl::string_view original_path = header_map.path();
  size_t length = original_path.size();
  auto path_ptr = std::make_unique<char[]>(length + 1); // auto free on return
  char* path = path_ptr.get();

  original_path.copy(path, length);

  // We rely on the string being null terminated so that we can safely look forward 1 character.
  path[length] = '\0';

  // Start normalizing the path.
  char* read = path;
  char* write = path;
  char* end = path + length;
  bool redirect = false;

  if (*read != '/') {
    // Reject relative paths
    return {HeaderValidator::RejectOrRedirectAction::Reject,
            UhvResponseCodeDetail::get().InvalidUrl};
  }

  ++read;
  ++write;

  //
  // Path normalization is based on RFC 3986:
  // https://datatracker.ietf.org/doc/html/rfc3986#section-3.3
  //
  // SPELLCHECKER(off)
  // path          = path-abempty    ; begins with "/" or is empty
  //               / path-absolute   ; begins with "/" but not "//"
  //               / path-noscheme   ; begins with a non-colon segment
  //               / path-rootless   ; begins with a segment
  //               / path-empty      ; zero characters
  //
  // path-abempty  = *( "/" segment )
  // path-absolute = "/" [ segment-nz *( "/" segment ) ]
  // path-noscheme = segment-nz-nc *( "/" segment )
  // path-rootless = segment-nz *( "/" segment )
  // path-empty    = 0<pchar>
  // segment       = *pchar
  // segment-nz    = 1*pchar
  // segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
  //               ; non-zero-length segment without any colon ":"
  //
  // pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
  // SPELLCHECKER(on)
  //
  while (read < end) {
    char ch = *read;
    char prev = *(write - 1);

    switch (ch) {
    case '%': {
      // Potential percent-encoded octet
      auto decode_result = normalizeAndDecodeOctet(read);
      switch (decode_result.result()) {
      case PercentDecodeResult::Invalid:
        ABSL_FALLTHROUGH_INTENDED;
      case PercentDecodeResult::Reject:
        // Reject the request
        return {HeaderValidator::RejectOrRedirectAction::Reject,
                UhvResponseCodeDetail::get().InvalidUrl};

      case PercentDecodeResult::Normalized:
        // Valid encoding but outside the UNRESERVED character set. The encoding was normalized to
        // UPPERCASE and the octet must not be decoded. Copy the normalized encoding.
        *write++ = *read++;
        *write++ = *read++;
        *write++ = *read++;
        break;

      case PercentDecodeResult::DecodedRedirect:
        // The encoding was properly decoded but, based on the config, the request should be
        // redirected to the normalized path.
        redirect = true;
        ABSL_FALLTHROUGH_INTENDED;
      case PercentDecodeResult::Decoded:
        // The encoding was decoded. Store the decoded octet in the last character of the percent
        // encoding (read[2]) so it will be processed in the next iteration.
        read += 2;
        *read = decode_result.octet();
      }
      break;
    }

    case '.': {
      // Potential "/./" or "/../" sequence
      if (*(read + 1) == '/' || (read + 1) == end) {
        // this is a "./" token.
        if (prev == '/') {
          // ignore "/./", jump to next segment (read + 2)
          read += 2;
        } else if (prev == '.' && *(write - 2) == '/') {
          // This is a "../" segment, remove the previous segment by back write up to the previous
          // slash (write - 2).
          write -= 2;
          if (write <= path) {
            // the full input is: "/.."", this is invalid
            return {HeaderValidator::RejectOrRedirectAction::Reject,
                    UhvResponseCodeDetail::get().InvalidUrl};
          }

          // reset write to overwrite the previous segment
          while (write > path && *(write - 1) != '/') {
            --write;
          }

          // skip the "../" token since it's been handled
          read += 2;
        } else {
          // just a dot within a normal path segment, copy it
          *write++ = *read++;
        }
      } else {
        // just a dot within a normal path segment, copy it
        *write++ = *read++;
      }
      break;
    }

    case '/': {
      if (prev == '/' && !config_.uri_path_normalization_options().skip_merging_slashes()) {
        // Duplicate slash, merge it
        ++read;
      } else {
        *write++ = *read++;
      }
      break;
    }

    default: {
      if (testChar(kPathHeaderCharTable, ch)) {
        // valid path character, copy it
        *write++ = *read++;
      } else {
        // invalid path character
        return {HeaderValidator::RejectOrRedirectAction::Reject,
                UhvResponseCodeDetail::get().InvalidUrl};
      }
    }
    }
  }

  *write = '\0';

  auto normalized_length = static_cast<size_t>(write - path);
  absl::string_view normalized_path{path, normalized_length};
  header_map.setPath(normalized_path);

  if (redirect) {
    return {HeaderValidator::RejectOrRedirectAction::Redirect,
            PathNormalizerResponseCodeDetail::get().RedirectNormalized};
  }

  return HeaderValidator::RequestHeaderMapValidationResult::success();
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
