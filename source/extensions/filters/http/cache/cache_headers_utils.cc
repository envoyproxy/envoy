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

std::ostream& operator<<(std::ostream& os, const OptionalDuration& duration) {
  return duration.has_value() ? os << duration.value().count() : os << " ";
}

// TODO: clean or remove
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

// TODO: clean or remove
std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control) {
  return os << "{"
            << "must_validate: " << response_cache_control.must_validate << ", "
            << "no_store: " << response_cache_control.no_store << ", "
            << "no_transform: " << response_cache_control.no_transform << ", "
            << "no_stale: " << response_cache_control.no_stale << ", "
            << "public: " << response_cache_control._public << ", "
            << "max_age: " << response_cache_control.max_age << "}";
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

OptionalDuration CacheHeadersUtils::parseDuration(absl::string_view s) {
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

std::pair<absl::string_view, absl::string_view>
CacheHeadersUtils::separateDirectiveAndArgument(absl::string_view full_directive) {
  return absl::StrSplit(absl::StripAsciiWhitespace(full_directive), absl::MaxSplits('=', 1));
}

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

RequestCacheControl CacheHeadersUtils::requestCacheControl(absl::string_view cache_control_header) {
  RequestCacheControl request_cache_control;
  std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      request_cache_control.must_validate = true;
    } else if (directive == "no-store") {
      request_cache_control.no_store = true;
    } else if (directive == "no-transform") {
      request_cache_control.no_transform = true;
    } else if (directive == "only-if-cached") {
      request_cache_control.only_if_cached = true;
    } else if (directive == "max-age") {
      request_cache_control.max_age = parseDuration(argument);
    } else if (directive == "min-fresh") {
      request_cache_control.min_fresh = parseDuration(argument);
    } else if (directive == "max-stale") {
      request_cache_control.max_stale =
          argument.empty() ? SystemTime::duration::max() : parseDuration(argument);
    }
  }
  return request_cache_control;
}

ResponseCacheControl
CacheHeadersUtils::responseCacheControl(absl::string_view cache_control_header) {
  ResponseCacheControl response_cache_control;
  std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      // If no-cache directive has arguments they are ignored - not handled
      response_cache_control.must_validate = true;
    } else if (directive == "must-revalidate" || directive == "proxy-revalidate") {
      response_cache_control.no_stale = true;
    } else if (directive == "no-store" || directive == "private") {
      // If private directive has arguments they are ignored - not handled
      response_cache_control.no_store = true;
    } else if (directive == "no-transform") {
      response_cache_control.no_transform = true;
    } else if (directive == "public") {
      response_cache_control._public = true;
    } else if (directive == "s-maxage") {
      response_cache_control.max_age = parseDuration(argument);
    } else if (!response_cache_control.max_age.has_value() && directive == "max-age") {
      response_cache_control.max_age = parseDuration(argument);
    }
  }
  return response_cache_control;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
