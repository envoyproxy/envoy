#include "extensions/filters/http/cache/cache_headers_utils.h"

#include <array>
#include <string>

#include "envoy/common/time.h"

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Utility functions used in RequestCacheControl & ResponseCacheControl.
namespace {
// A directive with an invalid duration is ignored, the RFC does not specify a behavior:
// https://httpwg.org/specs/rfc7234.html#delta-seconds
OptionalDuration parseDuration(absl::string_view s) {
  OptionalDuration duration;
  // Strip quotation marks if any.
  if (s.size() > 1 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  long num;
  if (absl::SimpleAtoi(s, &num) && num >= 0) {
    // s is a valid string of digits representing a positive number.
    duration = std::chrono::seconds(num);
  }
  return duration;
}

inline std::pair<absl::string_view, absl::string_view>
separateDirectiveAndArgument(absl::string_view full_directive) {
  return absl::StrSplit(absl::StripAsciiWhitespace(full_directive), absl::MaxSplits('=', 1));
}
} // namespace

// The grammar for This Cache-Control header value should be:
// Cache-Control   = 1#cache-directive
// cache-directive = token [ "=" ( token / quoted-string ) ]
// token           = 1*tchar
// tchar           = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+"
//                 / "-" / "." / "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
// quoted-string   = DQUOTE *( qdtext / quoted-pair ) DQUOTE
// qdtext          = HTAB / SP /%x21 / %x23-5B / %x5D-7E / obs-text
// obs-text        = %x80-FF
// quoted-pair     = "\" ( HTAB / SP / VCHAR / obs-text )
// VCHAR           =  %x21-7E  ; visible (printing) characters

// Multiple directives are comma separated according to:
// https://httpwg.org/specs/rfc7234.html#collected.abnf

RequestCacheControl::RequestCacheControl(absl::string_view cache_control_header) {
  const std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      must_validate_ = true;
    } else if (directive == "no-store") {
      no_store_ = true;
    } else if (directive == "no-transform") {
      no_transform_ = true;
    } else if (directive == "only-if-cached") {
      only_if_cached_ = true;
    } else if (directive == "max-age") {
      max_age_ = parseDuration(argument);
    } else if (directive == "min-fresh") {
      min_fresh_ = parseDuration(argument);
    } else if (directive == "max-stale") {
      max_stale_ = argument.empty() ? SystemTime::duration::max() : parseDuration(argument);
    }
  }
}

ResponseCacheControl::ResponseCacheControl(absl::string_view cache_control_header) {
  const std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      // If no-cache directive has arguments they are ignored - not handled.
      must_validate_ = true;
    } else if (directive == "must-revalidate" || directive == "proxy-revalidate") {
      no_stale_ = true;
    } else if (directive == "no-store" || directive == "private") {
      // If private directive has arguments they are ignored - not handled.
      no_store_ = true;
    } else if (directive == "no-transform") {
      no_transform_ = true;
    } else if (directive == "public") {
      is_public_ = true;
    } else if (directive == "s-maxage") {
      max_age_ = parseDuration(argument);
    } else if (!max_age_.has_value() && directive == "max-age") {
      max_age_ = parseDuration(argument);
    }
  }
}

bool operator==(const RequestCacheControl& lhs, const RequestCacheControl& rhs) {
  return (lhs.must_validate_ == rhs.must_validate_) && (lhs.no_store_ == rhs.no_store_) &&
         (lhs.no_transform_ == rhs.no_transform_) && (lhs.only_if_cached_ == rhs.only_if_cached_) &&
         (lhs.max_age_ == rhs.max_age_) && (lhs.min_fresh_ == rhs.min_fresh_) &&
         (lhs.max_stale_ == rhs.max_stale_);
}

bool operator==(const ResponseCacheControl& lhs, const ResponseCacheControl& rhs) {
  return (lhs.must_validate_ == rhs.must_validate_) && (lhs.no_store_ == rhs.no_store_) &&
         (lhs.no_transform_ == rhs.no_transform_) && (lhs.no_stale_ == rhs.no_stale_) &&
         (lhs.is_public_ == rhs.is_public_) && (lhs.max_age_ == rhs.max_age_);
}

SystemTime CacheHeadersUtils::httpTime(const Http::HeaderEntry* header_entry) {
  if (!header_entry) {
    return {};
  }
  absl::Time time;
  const std::string input(header_entry->value().getStringView());

  // Acceptable Date/Time Formats per:
  // https://tools.ietf.org/html/rfc7231#section-7.1.1.1
  //
  // Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate.
  // Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format.
  // Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format.
  static const char* rfc7231_date_formats[] = {"%a, %d %b %Y %H:%M:%S GMT",
                                               "%A, %d-%b-%y %H:%M:%S GMT", "%a %b %e %H:%M:%S %Y"};

  for (const std::string& format : rfc7231_date_formats) {
    if (absl::ParseTime(format, input, &time, nullptr)) {
      return ToChronoTime(time);
    }
  }
  return {};
}

absl::optional<uint64_t> CacheHeadersUtils::readAndRemoveLeadingDigits(absl::string_view& str) {
  uint64_t val = 0;
  uint32_t bytes_consumed = 0;

  for (const char cur : str) {
    if (!absl::ascii_isdigit(cur)) {
      break;
    }
    uint64_t new_val = (val * 10) + (cur - '0');
    if (new_val / 8 < val) {
      // Overflow occurred.
      return absl::nullopt;
    }
    val = new_val;
    ++bytes_consumed;
  }

  if (bytes_consumed) {
    // Consume some digits.
    str.remove_prefix(bytes_consumed);
    return val;
  }
  return absl::nullopt;
}

absl::flat_hash_set<std::string> VaryHeader::parseAllowlist() {
  // TODO(cbdm): Populate the hash_set from
  // envoy::extensions::filters::http::cache::v3alpha::CacheConfig::allowed_vary_headers.
  // Need to make sure that the headers we add here are valid values (i.e., not malformed). That
  // way, we won't have to check this again in isAllowed.
  return {"accept-encoding"};
}

bool VaryHeader::isAllowed(const Http::ResponseHeaderMap& headers) {
  if (noVary(headers)) {
    return true;
  }

  std::vector<std::string> varied_headers =
      parseHeaderValue(headers.get(Http::Headers::get().Vary));

  // If the vary value was malformed, it will not be contained in allowed_headers_.
  for (const std::string& header : varied_headers) {
    if (!allowed_headers_.contains(header)) {
      return false;
    }
  }

  return true;
}

bool VaryHeader::noVary(const Http::ResponseHeaderMap& headers) {
  const Http::HeaderEntry* vary_header = headers.get(Http::Headers::get().Vary);
  return ((!vary_header) || (vary_header->value().empty()));
}

std::string VaryHeader::createVaryKey(const Http::HeaderEntry* vary_header,
                                      const std::vector<const Http::HeaderEntry*>& entry_headers) {
  if (!vary_header) {
    return "";
  }

  ASSERT(vary_header->key() == "vary");

  std::string vary_key = "vary-key:";
  std::string vary_key_values;

  for (const std::string& header : parseHeaderValue(vary_header)) {
    absl::StrAppend(&vary_key, header, ";");

    for (const Http::HeaderEntry* cur_entry : entry_headers) {
      if (cur_entry->key() == header) {
        // TODO(cbdm): Can add some bucketing logic here based on header.
        absl::StrAppend(&vary_key_values, cur_entry->value().getStringView());
        break;
      }
    }

    absl::StrAppend(&vary_key_values, "\n");
  }

  absl::StrAppend(&vary_key, "\n", vary_key_values);

  return vary_key;
}

std::vector<std::string> VaryHeader::parseHeaderValue(const Http::HeaderEntry* vary_header) {
  if (!vary_header) {
    return {};
  }

  ASSERT(vary_header->key() == "vary");

  // Vary header value should follow rules set per:
  // https://tools.ietf.org/html/rfc7231#section-7.1.4

  std::vector<std::string> header_values =
      absl::StrSplit(vary_header->value().getStringView(), ',');
  for (std::string& value : header_values) {
    absl::StripAsciiWhitespace(&value);
    absl::AsciiStrToLower(&value);
  }

  return header_values;
}

std::vector<const Http::HeaderEntry*>
VaryHeader::possibleVariedHeaders(const Http::RequestHeaderMap& request_headers) {
  // TODO(cbdm): Could use a map for faster lookup when combining these values with the response
  // headers in createVaryKey.
  std::vector<const Http::HeaderEntry*> possible_headers;

  for (const std::string& header : allowed_headers_) {
    const Http::HeaderEntry* cur_header = request_headers.get(Http::LowerCaseString(header));

    if (cur_header) {
      possible_headers.emplace_back(cur_header);
    }
  }

  return possible_headers;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
