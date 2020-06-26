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

// Utility functions used in RequestCacheControl & ResponseCacheControl
namespace {
OptionalDuration parseDuration(absl::string_view s) {
  OptionalDuration duration;
  // Strip quotation marks if any
  if (!s.empty() && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  long num;
  if (absl::SimpleAtoi(s, &num) && num >= 0) {
    // s is a valid string of digits representing a positive number
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

RequestCacheControl::RequestCacheControl(bool must_validate, bool no_store, bool no_transform,
                                         bool only_if_cached, OptionalDuration max_age,
                                         OptionalDuration min_fresh, OptionalDuration max_stale)
    : must_validate(must_validate), no_store(no_store), no_transform(no_transform),
      only_if_cached(only_if_cached), max_age(max_age), min_fresh(min_fresh), max_stale(max_stale) {
}

RequestCacheControl::RequestCacheControl(absl::string_view cache_control_header) {
  must_validate = no_store = no_transform = only_if_cached = false;
  std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      must_validate = true;
    } else if (directive == "no-store") {
      no_store = true;
    } else if (directive == "no-transform") {
      no_transform = true;
    } else if (directive == "only-if-cached") {
      only_if_cached = true;
    } else if (directive == "max-age") {
      max_age = parseDuration(argument);
    } else if (directive == "min-fresh") {
      min_fresh = parseDuration(argument);
    } else if (directive == "max-stale") {
      max_stale = argument.empty() ? SystemTime::duration::max() : parseDuration(argument);
    }
  }
}

ResponseCacheControl::ResponseCacheControl(bool must_validate, bool no_store, bool no_transform,
                                           bool no_stale, bool is_public, OptionalDuration max_age)
    : must_validate(must_validate), no_store(no_store), no_transform(no_transform),
      no_stale(no_stale), is_public(is_public), max_age(max_age) {}

ResponseCacheControl::ResponseCacheControl(absl::string_view cache_control_header) {
  must_validate = no_store = no_transform = no_stale = is_public = false;
  std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      // If no-cache directive has arguments they are ignored - not handled
      must_validate = true;
    } else if (directive == "must-revalidate" || directive == "proxy-revalidate") {
      no_stale = true;
    } else if (directive == "no-store" || directive == "private") {
      // If private directive has arguments they are ignored - not handled
      no_store = true;
    } else if (directive == "no-transform") {
      no_transform = true;
    } else if (directive == "public") {
      is_public = true;
    } else if (directive == "s-maxage") {
      max_age = parseDuration(argument);
    } else if (!max_age.has_value() && directive == "max-age") {
      max_age = parseDuration(argument);
    }
  }
}

std::ostream& operator<<(std::ostream& os, const OptionalDuration& duration) {
  return duration.has_value() ? os << duration.value().count() : os << " ";
}

std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control) {
  return os << "{"
            << "must_validate: " << request_cache_control.must_validate << ", "
            << "no_store: " << request_cache_control.no_store << ", "
            << "no_transform: " << request_cache_control.no_transform << ", "
            << "only_if_cached: " << request_cache_control.only_if_cached << ", "
            << "max_age: " << request_cache_control.max_age << ", "
            << "min_fresh: " << request_cache_control.min_fresh << ", "
            << "max_stale: " << request_cache_control.max_stale << "}";
}

std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control) {
  return os << "{"
            << "must_validate: " << response_cache_control.must_validate << ", "
            << "no_store: " << response_cache_control.no_store << ", "
            << "no_transform: " << response_cache_control.no_transform << ", "
            << "no_stale: " << response_cache_control.no_stale << ", "
            << "public: " << response_cache_control.is_public << ", "
            << "max_age: " << response_cache_control.max_age << "}";
}

bool operator==(const RequestCacheControl& lhs, const RequestCacheControl& rhs) {
  return (lhs.must_validate == rhs.must_validate) && (lhs.no_store == rhs.no_store) &&
         (lhs.no_transform == rhs.no_transform) && (lhs.only_if_cached == rhs.only_if_cached) &&
         (lhs.max_age == rhs.max_age) && (lhs.min_fresh == rhs.min_fresh) &&
         (lhs.max_stale == rhs.max_stale);
}

bool operator==(const ResponseCacheControl& lhs, const ResponseCacheControl& rhs) {
  return (lhs.must_validate == rhs.must_validate) && (lhs.no_store == rhs.no_store) &&
         (lhs.no_transform == rhs.no_transform) && (lhs.no_stale == rhs.no_stale) &&
         (lhs.is_public == rhs.is_public) && (lhs.max_age == rhs.max_age);
}

SystemTime CacheHeadersUtils::httpTime(const Http::HeaderEntry* header_entry) {
  if (!header_entry) {
    return {};
  }
  absl::Time time;
  const std::string input(header_entry->value().getStringView());

  // Acceptable Date/Time Formats per
  // https://tools.ietf.org/html/rfc7231#section-7.1.1.1
  //
  // Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate
  // Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format
  // Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format
  static const auto& rfc7231_date_formats = *new std::array<std::string, 3>{
      "%a, %d %b %Y %H:%M:%S GMT", "%A, %d-%b-%y %H:%M:%S GMT", "%a %b %e %H:%M:%S %Y"};
  for (const std::string& format : rfc7231_date_formats) {
    if (absl::ParseTime(format, input, &time, nullptr)) {
      return ToChronoTime(time);
    }
  }
  return {};
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
