#pragma once

#include "envoy/common/time.h"
#include "envoy/http/header_map.h"

#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using OptionalDuration = absl::optional<SystemTime::duration>;

struct RequestCacheControl {
  // must_validate is true if 'no-cache' directive is present
  bool must_validate = false;
  bool no_store = false;
  // 'no-transform' directive is not used now
  bool no_transform = false;
  // 'only-if-cached' directive is not used now
  bool only_if_cached = false;
  OptionalDuration max_age;
  OptionalDuration min_fresh;
  OptionalDuration max_stale;

  bool operator==(const RequestCacheControl& rhs) const {
    return (must_validate == rhs.must_validate) && (no_store == rhs.no_store) &&
           (no_transform == rhs.no_transform) && (only_if_cached == rhs.only_if_cached) &&
           (max_age == rhs.max_age) && (min_fresh == rhs.min_fresh) && (max_stale == rhs.max_stale);
  }
};

// std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control);

struct ResponseCacheControl {
  // must_validate is true if 'no-cache' directive is present; arguments are ignored for now
  bool must_validate = false;
  // no_store is true if any of 'no-store' or 'private' directives is present.
  // 'private' arguments are ignored for now so it is equivalent to 'no-store'
  bool no_store = false;
  // 'no-transform' directive is not used now
  bool no_transform = false;
  // no_stale is true if any of 'must-revalidate' or 'proxy-revalidate' directives is present
  bool no_stale = false;
  // 'public' directive is not used now
  bool _public = false;
  // max_age is set if to 's-maxage' if present, if not it is set to 'max-age' if present.
  OptionalDuration max_age;

  bool operator==(const ResponseCacheControl& rhs) const {
    return (must_validate == rhs.must_validate) && (no_store == rhs.no_store) &&
           (no_transform == rhs.no_transform) && (max_age == rhs.max_age);
  }
};

// std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control);

// Could be merged with CacheFilterUtils as a single CacheUtils class
class CacheHeadersUtils {
public:
  // Parses header_entry as an HTTP time. Returns SystemTime() if
  // header_entry is null or malformed.
  static SystemTime httpTime(const Http::HeaderEntry* header_entry);

  // TODO: Add asserts as necessary
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

  // Parses the cache-control header of a request
  // A directive that should not have arguments will ignore any existing arguments
  // A directive that should have arguments will be ignored if no arguments are present
  // Unknown directives are ignored
  static RequestCacheControl requestCacheControl(absl::string_view cache_control_header);

  // Parses the cache-control header of a response
  // A directive that should not have arguments will ignore any existing arguments
  // A directive that should have arguments will be ignored if no arguments are present
  // Unknown directives are ignored
  static ResponseCacheControl responseCacheControl(absl::string_view cache_control_header);

private:
  // Parses a string_view as a quoted or unquoted duration, if s is not valid the OptionalDuration
  // has no value
  static OptionalDuration parseDuration(absl::string_view s);

  // Separates a directive into the directive literal and an argument (if any)
  static std::tuple<absl::string_view, absl::string_view>
  separateDirectiveAndArgument(absl::string_view full_directive);
};
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
