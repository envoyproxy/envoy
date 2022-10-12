#include "source/extensions/http/header_validators/envoy_default/path_normalizer.h"

#include "source/common/http/utility.h"
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

PathNormalizer::DecodedOctet
PathNormalizer::normalizeAndDecodeOctet(std::string::iterator iter,
                                        std::string::iterator end) const {
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

  if (iter == end || *iter != '%') {
    return {PercentDecodeResult::Invalid};
  }

  char ch = '\0';
  // Normalize and decode the octet
  for (int i = 0; i < 2; ++i) {
    ++iter;
    if (iter == end) {
      return {PercentDecodeResult::Invalid};
    }

    char nibble = *iter;
    if (!isxdigit(*iter)) {
      return {PercentDecodeResult::Invalid};
    }

    // normalize
    nibble = nibble >= 'a' ? nibble ^ 0x20 : nibble;
    *iter = nibble;

    // decode
    int factor = i == 0 ? 16 : 1;
    ch += factor * (nibble >= 'A' ? (nibble - 'A' + 10) : (nibble - '0'));
  }

  if (testChar(kUnreservedCharTable, ch)) {
    // Based on RFC, only decode characters in the UNRESERVED set.
    return {PercentDecodeResult::Decoded, ch};
  }

  if (ch == '/') {
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
 * first non-slash character after the preceding slash. For example:
 *
 *   path = "/hello/world/..";
 *                  ^    ^-- iterator
 *                  |-- start of previous segment
 *
 * The ``begin`` iterator is returned on error.
 */
std::string::iterator findStartOfPreviousSegment(std::string::iterator iter,
                                                 std::string::iterator begin) {
  bool seen_segment_char = false;
  for (; iter != begin; --iter) {
    if (*iter == '/' && seen_segment_char) {
      ++iter;
      return iter;
    }

    if (*iter != '/' && !seen_segment_char) {
      seen_segment_char = true;
    }
  }

  if (seen_segment_char) {
    ++begin;
  }

  return begin;
}

PathNormalizer::PathNormalizationResult
PathNormalizer::normalizePathUri(RequestHeaderMap& header_map) const {
  // Attempt to parse the request-target to see if it is in origin-form. We are only normalizing
  // the path component of the request-target, so if is is origin or absolute form we will extract
  // the path. From RFC 9112, https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2:
  //
  // request-target = origin-form
  //                / absolute-form
  //                / authority-form
  //                / asterisk-form
  //
  // origin-form    = absolute-path [ "?" query ]
  // absolute-form  = absolute-URI
  // authority-form = uri-host ":" port
  // asterisk-form  = "*"
  const auto original_uri = header_map.path();
  if (original_uri == "*") {
    // Asterisk form
    return PathNormalizationResult::success();
  }

  const bool is_connect_method =
      header_map.method() == ::Envoy::Http::Headers::get().MethodValues.Connect;
  ::Envoy::Http::Utility::Url url;
  const bool is_origin_form = !url.initialize(original_uri, is_connect_method);
  // If is_origin_form==true, then the original_uri is treated as origin-form and must begin with
  // a "/" character.
  if (!is_origin_form && url.pathAndQueryParams().empty() && is_connect_method) {
    // CONNECT requests must be in authority-form with no path specified.
    return PathNormalizationResult::success();
  }

  // Make a copy of the original path and then create a readonly string_view to it. The string_view
  // is used for optimized sub-strings and the path is modified in place.
  absl::string_view original_path = is_origin_form ? original_uri : url.pathAndQueryParams();
  std::string path{original_path.data(), original_path.length()};
  absl::string_view path_view{path};

  // Start normalizing the path.
  const auto begin = path.begin();
  auto read = path.begin();
  auto write = path.begin();
  auto end = path.end();
  bool redirect = false;

  if (read == end || *read != '/') {
    // Reject empty or relative paths
    return {PathNormalizationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
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
  while (read != end) {
    char ch = *read;
    char prev = *std::prev(write);

    switch (ch) {
    case '%': {
      auto decode_result = normalizeAndDecodeOctet(read, end);
      switch (decode_result.result()) {
      case PercentDecodeResult::Invalid:
        ABSL_FALLTHROUGH_INTENDED;
      case PercentDecodeResult::Reject:
        // Reject the request
        return {PathNormalizationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};

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
        // encoding (read[2]) so it will be processed in the next iteration. We can safely advance
        // 2 positions since we know that the value was correctly decoded.
        std::advance(read, 2);
        *read = decode_result.octet();
      }
      break;
    }

    case '.': {
      if (prev == '/') {
        // attempt to read ahead 2 characters to see if we are in a "./" or "../" segment.
        const auto dot_segment = path_view.substr(std::distance(begin, read), 3);
        if (absl::StartsWith(dot_segment, "./") || dot_segment == ".") {
          // This is a "/./" segment or the path is terminated by "/.", ignore it
          size_t distance = std::min<size_t>(dot_segment.size(), 2);
          // Advance the read iterator by 1 if the path ends with "." or 2 if the segment is "./"
          std::advance(read, distance);
        } else if (dot_segment == "../" || dot_segment == "..") {
          // This is a "/../" segment or the path is terminated by "/..", navigate one segment up.
          // Back up write 1 position to the previous slash to find the previous segment start.
          auto new_write = findStartOfPreviousSegment(std::prev(write), begin);
          if (new_write == begin) {
            // This is an invalid ".." segment, most likely the full path is "/..", which attempts
            // to go above the root.
            return {PathNormalizationResult::Action::Reject,
                    UhvResponseCodeDetail::get().InvalidUrl};
          }

          // Set the write position to overwrite the previous segment
          write = new_write;
          // Advance the read iterator by 2 if the path ends with ".." or 3 if the segment is "../"
          size_t distance = std::min<size_t>(dot_segment.size(), 3);
          std::advance(read, distance);
        } else {
          *write++ = *read++;
        }
      } else {
        *write++ = *read++;
      }

      break;
    }

    case '/': {
      if (prev == '/' && !config_.uri_path_normalization_options().skip_merging_slashes()) {
        // Duplicate slash, merge it
        ++read;
      } else {
        // Not a duplicate slash or we aren't configured to merge slashes, copy it
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
        return {PathNormalizationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
      }
    }
    }
  }

  absl::string_view normalized_path = path_view.substr(0, std::distance(begin, write));
  if (is_origin_form) {
    // origin-form is the absolute path, set it
    header_map.setPath(normalized_path);
  } else {
    // absolute and authority forms have a prefix that we need to keep
    auto path_begin_index = original_uri.length() - original_path.length();
    absl::string_view prefix;
    if (original_uri.at(path_begin_index) == '/') {
      // absolute-form
      prefix = original_uri.substr(0, path_begin_index);
    } else {
      // The URL class sets the path to "/" if the path is empty for authority-form, which we
      // detect if the first path character is not a "/". The authority-form is our entire prefix.
      prefix = original_uri;
    }

    header_map.setPath(absl::StrCat(prefix, normalized_path));
  }

  if (redirect) {
    return {PathNormalizationResult::Action::Redirect,
            PathNormalizerResponseCodeDetail::get().RedirectNormalized};
  }

  return PathNormalizationResult::success();
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
