#include "extensions/filters/http/cache/http_cache_utils.h"

#include <array>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/strip.h"

using absl::ascii_isalnum;
using absl::ascii_isdigit;
using absl::Duration;
using absl::InfiniteDuration;
using absl::InfinitePast;
using absl::ParseTime;
using absl::Seconds;
using absl::SimpleAtoi;
using absl::string_view;
using absl::Time;
using absl::ZeroDuration;
using Envoy::Http::HeaderEntry;
using std::array;
using std::string;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace Internal {

namespace {

bool tchar(char c) {
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
  return ascii_isalnum(c);
}

bool eatToken(string_view& s) {
  const string_view::iterator token_end = c_find_if_not(s, &tchar);
  if (token_end == s.begin()) {
    return false;
  }
  s.remove_prefix(token_end - s.begin());
  return true;
}

void eatDirectiveArgument(string_view& s) {
  if (s.empty()) {
    return;
  }
  if (s.front() == '"') {
    // TODO(toddmgreer) handle \-escaped quotes
    const size_t closing_quote = s.find('"', 1);
    s.remove_prefix(closing_quote);
  } else {
    eatToken(s);
  }
}
} // namespace

// If s is nonnull and begins with decimal digits, return Eat leading digits in
// *s, if any
Duration eatLeadingDuration(string_view* s) {
  const string_view::iterator digits_end = c_find_if_not(*s, &ascii_isdigit);
  const size_t digits_length = digits_end - s->begin();
  if (digits_length == 0) {
    return ZeroDuration();
  }
  const string_view digits(s->begin(), digits_length);
  s->remove_prefix(digits_length);
  uint64_t num;
  return SimpleAtoi(digits, &num) ? Seconds(num) : InfiniteDuration();
}

Duration effectiveMaxAge(string_view cache_control) {
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
  Duration max_age = -InfiniteDuration();
  bool found_s_maxage = false;
  while (!cache_control.empty()) {
    // Each time through the loop, we eat one cache-directive. Each branch
    // either returns or completely eats a cache-directive.
    if (ConsumePrefix(&cache_control, "no-cache")) {
      if (eatToken(cache_control)) {
        // The token wasn't no-cache; it just started that way, so we must
        // finish eating this cache-directive.
        if (ConsumePrefix(&cache_control, "=")) {
          eatDirectiveArgument(cache_control);
        }
      } else {
        // Found a no-cache directive, so validation is required.
        return -InfiniteDuration();
      }
    } else if (ConsumePrefix(&cache_control, "s-maxage=")) {
      max_age = eatLeadingDuration(&cache_control);
      found_s_maxage = true;
      cache_control = StripLeadingAsciiWhitespace(cache_control);
      if (!cache_control.empty() && cache_control[0] != ',') {
        // Unexpected text at end of directive
        return -InfiniteDuration();
      }
    } else if (!found_s_maxage && ConsumePrefix(&cache_control, "max-age=")) {
      max_age = eatLeadingDuration(&cache_control);
      if (!cache_control.empty() && cache_control[0] != ',') {
        // Unexpected text at end of directive
        return -InfiniteDuration();
      }
    } else if (eatToken(cache_control)) {
      // Unknown directive--ignore.
      if (ConsumePrefix(&cache_control, "=")) {
        eatDirectiveArgument(cache_control);
      }
    } else {
      // This directive starts with illegal characters. Require validation.
      return -InfiniteDuration();
    }
    // Whichever branch we took should have consumed the entire cache-directive,
    // so we just need to eat the delimiter and optional whitespace.
    ConsumePrefix(&cache_control, ",");
    cache_control = StripLeadingAsciiWhitespace(cache_control);
  }
  return max_age;
}

Time httpTime(const HeaderEntry* header_entry) {
  if (!header_entry) {
    return InfinitePast();
  }
  Time time;
  const string input(header_entry->value().getStringView());

  // RFC 7231 7.1.1.1: Acceptable Date/Time Formats:
  // Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate
  // Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format
  // Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format
  const array<string, 3> rfc7231_date_formats = {
      "%a, %d %b %Y %H:%M:%S GMT", "%A, %d-%b-%y %H:%M:%S GMT", "%a %b %e %H:%M:%S %Y"};
  for (const string& format : rfc7231_date_formats) {
    if (ParseTime(format, input, &time, nullptr)) {
      return time;
    }
  }
  return InfinitePast();
}
} // namespace Internal
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
