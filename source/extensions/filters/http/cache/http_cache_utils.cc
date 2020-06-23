#include "extensions/filters/http/cache/http_cache_utils.h"

#include <array>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// True for characters defined as tchars by
// https://tools.ietf.org/html/rfc7230#section-3.2.6
//
// tchar           = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+"
//                 / "-" / "." / "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
bool HttpCacheUtils::tchar(char c) {
  switch (c) {
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '*':
  case '+':
  case '-':
  case '.':
  case '^':
  case '_':
  case '`':
  case '|':
  case '~':
    return true;
  }
  return absl::ascii_isalnum(c);
}

// Removes an initial HTTP header field value token, as defined by
// https://tools.ietf.org/html/rfc7230#section-3.2.6. Returns true if an initial
// token was present.
//
// token           = 1*tchar
bool HttpCacheUtils::eatToken(absl::string_view& s) {
  const absl::string_view::iterator token_end = absl::c_find_if_not(s, &tchar);
  if (token_end == s.begin()) {
    return false;
  }
  s.remove_prefix(token_end - s.begin());
  return true;
}

// Removes an initial token or quoted-string (if present), as defined by
// https://tools.ietf.org/html/rfc7234#section-5.2. If a cache-control directive
// has an argument (as indicated by '='), it should be in this form.
//
// quoted-string   = DQUOTE *( qdtext / quoted-pair ) DQUOTE
// qdtext          = HTAB / SP /%x21 / %x23-5B / %x5D-7E / obs-text
// obs-text        = %x80-FF
// quoted-pair     = "\" ( HTAB / SP / VCHAR / obs-text )
// VCHAR           =  %x21-7E  ; visible (printing) characters
//
// For example, the directive "my-extension=42" has an argument of "42", so an
// input of "public, my-extension=42, max-age=999"
void HttpCacheUtils::eatDirectiveArgument(absl::string_view& s) {
  if (s.empty()) {
    return;
  }
  if (s.front() == '"') {
    // TODO(#9833): handle \-escaped quotes
    const size_t closing_quote = s.find('"', 1);
    s.remove_prefix(closing_quote);
  } else {
    eatToken(s);
  }
}

// If s is non-null and begins with a decimal number ([0-9]+), removes it from
// the input and returns a SystemTime::duration representing that many seconds.
// If s is null or doesn't begin with digits, returns
// SystemTime::duration::zero(). If parsing overflows, returns
// SystemTime::duration::max().
SystemTime::duration HttpCacheUtils::eatLeadingDuration(absl::string_view& s) {
  const absl::string_view::iterator digits_end = absl::c_find_if_not(s, &absl::ascii_isdigit);
  const size_t digits_length = digits_end - s.begin();
  if (digits_length == 0) {
    return SystemTime::duration::zero();
  }
  const absl::string_view digits(s.begin(), digits_length);
  s.remove_prefix(digits_length);
  uint64_t num;
  return absl::SimpleAtoi(digits, &num) ? std::chrono::seconds(num) : SystemTime::duration::max();
}

// Returns the effective max-age represented by cache-control. If the result is
// SystemTime::duration::zero(), or is less than the response's, the response
// should be validated.
//
// TODO(#9833): Write a CacheControl class to fully parse the cache-control
// header value. Consider sharing with the gzip filter.
SystemTime::duration HttpCacheUtils::effectiveMaxAge(absl::string_view cache_control) {
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
  SystemTime::duration max_age = SystemTime::duration::zero();
  bool found_s_maxage = false;
  while (!cache_control.empty()) {
    // Each time through the loop, we eat one cache-directive. Each branch
    // either returns or completely eats a cache-directive.
    if (absl::ConsumePrefix(&cache_control, "no-cache")) {
      if (eatToken(cache_control)) {
        // The token wasn't no-cache; it just started that way, so we must
        // finish eating this cache-directive.
        if (absl::ConsumePrefix(&cache_control, "=")) {
          eatDirectiveArgument(cache_control);
        }
      } else {
        // Found a no-cache directive, so validation is required.
        return SystemTime::duration::zero();
      }
    } else if (absl::ConsumePrefix(&cache_control, "s-maxage=")) {
      max_age = eatLeadingDuration(cache_control);
      found_s_maxage = true;
      cache_control = absl::StripLeadingAsciiWhitespace(cache_control);
      if (!cache_control.empty() && cache_control[0] != ',') {
        // Unexpected text at end of directive
        return SystemTime::duration::zero();
      }
    } else if (!found_s_maxage && absl::ConsumePrefix(&cache_control, "max-age=")) {
      max_age = eatLeadingDuration(cache_control);
      if (!cache_control.empty() && cache_control[0] != ',') {
        // Unexpected text at end of directive
        return SystemTime::duration::zero();
      }
    } else if (eatToken(cache_control)) {
      // Unknown directive--ignore.
      if (absl::ConsumePrefix(&cache_control, "=")) {
        eatDirectiveArgument(cache_control);
      }
    } else {
      // This directive starts with illegal characters. Require validation.
      return SystemTime::duration::zero();
    }
    // Whichever branch we took should have consumed the entire cache-directive,
    // so we just need to eat the delimiter and optional whitespace.
    absl::ConsumePrefix(&cache_control, ",");
    cache_control = absl::StripLeadingAsciiWhitespace(cache_control);
  }
  return max_age;
}

SystemTime HttpCacheUtils::httpTime(const Http::HeaderEntry* header_entry) {
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
