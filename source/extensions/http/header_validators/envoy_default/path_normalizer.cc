#include "source/extensions/http/header_validators/envoy_default/path_normalizer.h"

#include "envoy/http/header_validator_errors.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::envoy::extensions::http::header_validators::envoy_default::v3::
    HeaderValidatorConfig_UriPathNormalizationOptions;
using ::Envoy::Http::HeaderUtility;
using ::Envoy::Http::PathNormalizerResponseCodeDetail;
using ::Envoy::Http::RequestHeaderMap;
using ::Envoy::Http::testCharInTable;
using ::Envoy::Http::UhvResponseCodeDetail;

PathNormalizer::PathNormalizer(const HeaderValidatorConfig& config,
                               const ConfigOverrides& config_overrides)
    : config_(config), config_overrides_(config_overrides) {}

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

  const bool preserve_case = config_overrides_.preserve_url_encoded_case_;

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
    if (!preserve_case) {
      *iter = nibble;
    }

    // decode
    int factor = i == 0 ? 16 : 1;
    ch += factor * (nibble >= 'A' ? (nibble - 'A' + 10) : (nibble - '0'));
  }

  if (testCharInTable(kUnreservedCharTable, ch)) {
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
      ENVOY_BUG(false, "Unexpected path_with_escaped_slashes_action");
      break;
    }
  }

  // The octet is a valid encoding but it wasn't be decoded because it was outside the UNRESERVED
  // character set.
  return {PercentDecodeResult::Normalized};
}

/*
 * Find the start of the previous segment within the path. The start of the previous segment is the
 * first non-slash character that directly follows a slash. For example:
 *
 *   path = "/hello/world/..";
 *           ^      ^    ^-- current argument
 *           |      |-- start of previous segment (return value)
 *           |-- begin argument
 *
 * Duplicate slashes that are encountered are ignored. For example:
 *
 * path = "/parent//child////..";
 *                  ^       ^-- current argument
 *                  |-- start of previous segment
 *
 * The ``current`` argument must point to a slash character. The ``begin`` iterator must be the
 * start of the path and it is returned on error.
 */
std::string::iterator findStartOfPreviousSegment(std::string::iterator current,
                                                 std::string::iterator begin) {
  bool seen_segment_char = false;
  for (; current != begin; --current) {
    if (*current == '/' && seen_segment_char) {
      ++current;
      return current;
    }

    if (*current != '/' && !seen_segment_char) {
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
  // Parse and normalize the :path header and update it in the map. From RFC 9112,
  // https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2:
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
  //
  // TODO(#23887) - potentially separate path normalization into multiple independent operations.
  const auto original_path = header_map.path();
  if (original_path == "*" &&
      header_map.method() == ::Envoy::Http::Headers::get().MethodValues.Options) {
    // asterisk-form, only valid for OPTIONS request
    return PathNormalizationResult::success();
  }

  if (HeaderUtility::isStandardConnectRequest(header_map)) {
    // The :path can only be empty for standard CONNECT methods, where the request-target is in
    // authority-form for HTTP/1 requests, or :path is empty for HTTP/2 requests.
    if (original_path.empty()) {
      return PathNormalizationResult::success();
    }
    return {PathNormalizationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
  }

  if (original_path.empty() || original_path.at(0) != '/') {
    return {PathNormalizationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
  }

  // Split the path and the query parameters / fragment component.
  auto [path_view, query] = splitPathAndQueryParams(original_path);
  // Make a copy of the original path and then create a readonly string_view to it. The string_view
  // is used for optimized sub-strings and the path is modified in place.
  std::string path{path_view.data(), path_view.length()};

  // Start normalizing the path.
  bool redirect = false;

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
  {
    // pass 1: normalize and decode percent-encoded octets
    const auto result = decodePass(path);
    if (result.action() == PathNormalizationResult::Action::Reject) {
      return result;
    }

    redirect |= result.action() == PathNormalizationResult::Action::Redirect;
  }

  // The `envoy.uhv.allow_non_compliant_characters_in_path` flag allows the \ (back slash)
  // character, which legacy path normalization was changing to / (forward slash).
  if (config_overrides_.allow_non_compliant_characters_in_path_) {
    translateBackToForwardSlashes(path);
  }

  if (!config_.uri_path_normalization_options().skip_merging_slashes()) {
    // pass 2: merge duplicate slashes (if configured to do so)
    const auto result = mergeSlashesPass(path);
    if (result.action() == PathNormalizationResult::Action::Reject) {
      return result;
    }

    redirect |= result.action() == PathNormalizationResult::Action::Redirect;
  }

  {
    // pass 3: collapse dot and dot-dot segments
    const auto result = collapseDotSegmentsPass(path);
    if (result.action() == PathNormalizationResult::Action::Reject) {
      return result;
    }

    redirect |= result.action() == PathNormalizationResult::Action::Redirect;
  }

  absl::string_view normalized_path{path};
  // Update the :path header. We need to honor the normalized path and the original query/fragment
  // components.
  header_map.setPath(absl::StrCat(normalized_path, query));

  if (redirect) {
    return {PathNormalizationResult::Action::Redirect,
            ::Envoy::Http::PathNormalizerResponseCodeDetail::get().RedirectNormalized};
  }

  return PathNormalizationResult::success();
}

void PathNormalizer::translateBackToForwardSlashes(std::string& path) const {
  for (char& character : path) {
    if (character == '\\') {
      character = '/';
    }
  }
}

PathNormalizer::PathNormalizationResult PathNormalizer::decodePass(std::string& path) const {
  auto begin = path.begin();
  auto read = std::next(begin);
  auto write = std::next(begin);
  auto end = path.end();
  bool redirect = false;
  const bool allow_invalid_url_encoding =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.uhv_allow_malformed_url_encoding");

  while (read != end) {
    if (*read == '%') {
      auto decode_result = normalizeAndDecodeOctet(read, end);
      // TODO(#23885) - add and honor config to not reject invalid percent-encoded octets.
      switch (decode_result.result()) {
      case PercentDecodeResult::Invalid:
        if (allow_invalid_url_encoding) {
          // Write the % character that starts invalid URL encoded sequence and then continue
          // scanning from the next character.
          *write++ = *read++;
          break;
        }
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
    } else {
      *write++ = *read++;
    }
  }

  path.resize(std::distance(begin, write));
  if (redirect) {
    return {PathNormalizationResult::Action::Redirect,
            ::Envoy::Http::PathNormalizerResponseCodeDetail::get().RedirectNormalized};
  }

  return PathNormalizationResult::success();
}

PathNormalizer::PathNormalizationResult PathNormalizer::mergeSlashesPass(std::string& path) const {
  auto begin = path.begin();
  auto read = std::next(begin);
  auto write = std::next(begin);
  auto end = path.end();

  while (read != end) {
    if (*read == '/') {
      char prev = *std::prev(write);
      if (prev == '/') {
        // Duplicate slash, merge it
        ++read;
      } else {
        // Not a duplicate slash
        *write++ = *read++;
      }
    } else {
      *write++ = *read++;
    }
  }

  path.resize(std::distance(begin, write));
  return PathNormalizationResult::success();
}

PathNormalizer::PathNormalizationResult
PathNormalizer::collapseDotSegmentsPass(std::string& path) const {
  auto begin = path.begin();
  auto read = std::next(begin);
  auto write = std::next(begin);
  auto end = path.end();
  absl::string_view path_view{path};

  while (read != end) {
    if (*read == '.') {
      char prev = *std::prev(write);
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
    } else {
      *write++ = *read++;
    }
  }

  path.resize(std::distance(begin, write));
  return PathNormalizationResult::success();
}

std::tuple<absl::string_view, absl::string_view>
PathNormalizer::splitPathAndQueryParams(absl::string_view path_and_query_params) const {
  // Split on the query (?) or fragment (#) delimiter, whichever one is first.
  // TODO(#23886) - add and honor config option for handling the path fragment component.
  auto delim = path_and_query_params.find_first_of("?#");
  if (delim == absl::string_view::npos) {
    // no query/fragment component
    return std::make_tuple(path_and_query_params, "");
  }

  return std::make_tuple(path_and_query_params.substr(0, delim),
                         path_and_query_params.substr(delim));
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
