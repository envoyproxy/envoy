#include "extensions/filters/http/cache/cache_headers_utils.h"

#include <bits/stdint-intn.h>
#include <bits/stdint-uintn.h>

#include <array>
#include <string>
#include <tuple>

#include "envoy/common/time.h"

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// TODO: clean or remove
// std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control) {
//   return os << request_cache_control.must_validate << " " << request_cache_control.no_store << "
//   " << request_cache_control.no_transform << " " << request_cache_control.only_if_cached << " "
//   <<
//                (request_cache_control.max_age.has_value() ?
//                request_cache_control.max_age.value().count() : -1) << " " <<
//                (request_cache_control.min_fresh.has_value() ?
//                request_cache_control.min_fresh.value().count() : -1)  << " " <<
//                (request_cache_control.max_stale.has_value() ?
//                request_cache_control.max_stale.value().count() : -1);
// }

// TODO: clean or remove
// std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control) {
//   return os << response_cache_control.must_validate << " " << response_cache_control.no_store <<
//   " " << response_cache_control.no_transform << " " <<
//                (response_cache_control.max_age.has_value() ?
//                response_cache_control.max_age.value().count() : -1);
// }

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

std::tuple<absl::string_view, absl::string_view>
CacheHeadersUtils::separateDirectiveAndArgument(absl::string_view full_directive) {
  std::vector<absl::string_view> directive_parts =
      absl::StrSplit(full_directive, absl::MaxSplits('=', 1));
  absl::string_view directive = absl::StripAsciiWhitespace(directive_parts[0]);
  absl::string_view argument =
      directive_parts.size() > 1 ? absl::StripAsciiWhitespace(directive_parts[1]) : "";
  return std::make_tuple(directive, argument);
}

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

    if (directive == "no-cache" || directive == "must-revalidate" ||
        directive == "proxy-revalidate") {
      // If no-cache directive has arguments they are ignored - not handled
      response_cache_control.must_validate = true;
    } else if (directive == "no-store" || directive == "private") {
      // If private directive has arguments they are ignored - not handled
      response_cache_control.no_store = true;
    } else if (directive == "no-transform") {
      response_cache_control.no_transform = true;
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
