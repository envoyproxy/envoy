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

PathNormalizer::DecodedOctet PathNormalizer::normalizeAndDecodeOctet(std::string& str,
                                                                     std::size_t pos) const {
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
  auto octet = absl::string_view(str).substr(pos, 3);
  if (octet.size() < 3 || octet.at(0) != '%' || !isxdigit(octet.at(1)) || !isxdigit(octet.at(2))) {
    // invalid percent encoded octet
    return {PercentDecodeResult::Invalid};
  }

  char ch;

  // normalize to UPPERCASE
  str[pos + 1] = octet[1] >= 'a' && octet[1] <= 'z' ? octet[1] ^ 0x20 : octet[1];
  str[pos + 2] = octet[2] >= 'a' && octet[2] <= 'z' ? octet[2] ^ 0x20 : octet[2];

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

/*
 * Find the start index of the previous segment within the path. The previous segment starts at the
 * first non-slash character after the preceeding slash. For example:
 *
 *   path = "/hello/world/..";
 *                  ^    ^-- pos
 *                  |-- start of previous segment
 */
size_t findStartOfPreviousSegment(absl::string_view path, size_t pos) {
  bool seen_segment_char = false;
  for (ssize_t i = pos; i >= 0; --i) {
    if (path.at(i) == '/' && seen_segment_char) {
      return i + 1;
    }

    if (path.at(i) != '/' && !seen_segment_char) {
      seen_segment_char = true;
    }
  }

  return absl::string_view::npos;
}

HeaderValidator::RequestHeaderMapValidationResult
PathNormalizer::normalizePathUri(RequestHeaderMap& header_map) const {
  // Make a copy of the original path so we can edit it in place.
  absl::string_view original_path = header_map.path();
  // make a copy of the original path and then create a readonly string_view to it. The path
  // is modified in place.
  std::string path{original_path.data(), original_path.length()};
  absl::string_view path_view{path};

  // Start normalizing the path.
  size_t read = 0;
  size_t write = 0;
  size_t end = original_path.size();
  bool redirect = false;

  if (path_view.at(0) != '/') {
    // Reject relative paths
    return {HeaderValidator::RejectOrRedirectAction::Reject,
            UhvResponseCodeDetail::get().InvalidUrl};
  }

  ++read;
  ++write;

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
  while (read < end) {
    char ch = path_view.at(read);
    char prev = path_view.at(write - 1);

    switch (ch) {
    case '%': {
      auto decode_result = normalizeAndDecodeOctet(path, read);
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
        path[write++] = path_view.at(read++);
        path[write++] = path_view.at(read++);
        path[write++] = path_view.at(read++);
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
        path[read] = decode_result.octet();
      }
      break;
    }

    case '.': {
      if (prev == '/') {
        // attempt to read ahead 2 characters to see if we are in a "./" or "../" segment.
        auto dot_segment = path_view.substr(read, 3);
        if (absl::StartsWith(dot_segment, "./") || dot_segment == ".") {
          // This is a "/./" segment or the path is terminated by "/.", ignore it
          read += 2;
        } else if (dot_segment == "../" || dot_segment == "..") {
          // This is a "/../" segment or the path is terminated by "/..", navigate one segment up
          auto new_write = findStartOfPreviousSegment(path, write - 1);
          if (new_write == absl::string_view::npos) {
            // This is an invalid ".." segment, most likely the full path is "/..", which attempts
            // to go above the root.
            return {HeaderValidator::RejectOrRedirectAction::Reject,
                    UhvResponseCodeDetail::get().InvalidUrl};
          }

          // Set the write position to overwrite the previous segment
          write = new_write;
          read += 3;
        } else {
          path[write++] = path_view.at(read++);
        }
      } else {
        path[write++] = path_view.at(read++);
      }

      break;
    }

    case '/': {
      if (prev == '/' && !config_.uri_path_normalization_options().skip_merging_slashes()) {
        // Duplicate slash, merge it
        ++read;
      } else {
        path[write++] = path_view.at(read++);
      }
      break;
    }

    default: {
      if (testChar(kPathHeaderCharTable, ch)) {
        // valid path character, copy it
        path[write++] = path_view.at(read++);
      } else {
        // invalid path character
        return {HeaderValidator::RejectOrRedirectAction::Reject,
                UhvResponseCodeDetail::get().InvalidUrl};
      }
    }
    }
  }

  absl::string_view normalized_path = path_view.substr(0, write);
  header_map.setPath(normalized_path);

  // redirect |= normalized_path != original_path;
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
