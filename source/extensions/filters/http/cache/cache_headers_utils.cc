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
// A directive with an invalid duration is ignored, the RFC does not specify a behavior:
// https://httpwg.org/specs/rfc7234.html#delta-seconds
OptionalDuration parseDuration(absl::string_view s) {
  OptionalDuration duration;
  // Strip quotation marks if any
  if (s.size() > 1 && s.front() == '"' && s.back() == '"') {
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
      // If no-cache directive has arguments they are ignored - not handled
      must_validate_ = true;
    } else if (directive == "must-revalidate" || directive == "proxy-revalidate") {
      no_stale_ = true;
    } else if (directive == "no-store" || directive == "private") {
      // If private directive has arguments they are ignored - not handled
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

std::ostream& operator<<(std::ostream& os, const OptionalDuration& duration) {
  return duration.has_value() ? os << duration.value().count() : os << " ";
}

std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control) {
  return os << "{"
            << "must_validate: " << request_cache_control.must_validate_ << ", "
            << "no_store: " << request_cache_control.no_store_ << ", "
            << "no_transform: " << request_cache_control.no_transform_ << ", "
            << "only_if_cached: " << request_cache_control.only_if_cached_ << ", "
            << "max_age: " << request_cache_control.max_age_ << ", "
            << "min_fresh: " << request_cache_control.min_fresh_ << ", "
            << "max_stale: " << request_cache_control.max_stale_ << "}";
}

std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control) {
  return os << "{"
            << "must_validate: " << response_cache_control.must_validate_ << ", "
            << "no_store: " << response_cache_control.no_store_ << ", "
            << "no_transform: " << response_cache_control.no_transform_ << ", "
            << "no_stale: " << response_cache_control.no_stale_ << ", "
            << "public: " << response_cache_control.is_public_ << ", "
            << "max_age: " << response_cache_control.max_age_ << "}";
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

  // Acceptable Date/Time Formats per
  // https://tools.ietf.org/html/rfc7231#section-7.1.1.1
  //
  // Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate
  // Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format
  // Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format
  static const char* rfc7231_date_formats[] = {"%a, %d %b %Y %H:%M:%S GMT",
                                               "%A, %d-%b-%y %H:%M:%S GMT", "%a %b %e %H:%M:%S %Y"};

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
