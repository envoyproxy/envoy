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
  RequestCacheControl(bool must_validate = false, bool no_store = false, bool no_transform = false,
                      bool only_if_cached = false, OptionalDuration max_age = OptionalDuration(),
                      OptionalDuration min_fresh = OptionalDuration(),
                      OptionalDuration max_stale = OptionalDuration());
  RequestCacheControl(absl::string_view cache_control_header);

  // must_validate is true if 'no-cache' directive is present
  bool must_validate : 1;
  bool no_store : 1;
  // 'no-transform' directive is not used now
  bool no_transform : 1;
  // 'only-if-cached' directive is not used now
  bool only_if_cached : 1;
  OptionalDuration max_age;
  OptionalDuration min_fresh;
  OptionalDuration max_stale;
};

struct ResponseCacheControl {
  ResponseCacheControl(bool must_validate = false, bool no_store = false, bool no_transform = false,
                       bool no_stale = false, bool is_public = false,
                       OptionalDuration max_age = OptionalDuration());
  ResponseCacheControl(absl::string_view cache_control_header);

  // must_validate is true if 'no-cache' directive is present; arguments are ignored for now
  bool must_validate : 1;
  // no_store is true if any of 'no-store' or 'private' directives is present.
  // 'private' arguments are ignored for now so it is equivalent to 'no-store'
  bool no_store : 1;
  // 'no-transform' directive is not used now
  bool no_transform : 1;
  // no_stale is true if any of 'must-revalidate' or 'proxy-revalidate' directives is present
  bool no_stale : 1;
  // 'public' directive is not used now
  bool is_public : 1;
  // max_age is set if to 's-maxage' if present, if not it is set to 'max-age' if present.
  OptionalDuration max_age;
};

std::ostream& operator<<(std::ostream& os, const OptionalDuration& duration);
std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control);
std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control);
bool operator==(const RequestCacheControl& lhs, const RequestCacheControl& rhs);
bool operator==(const ResponseCacheControl& lhs, const ResponseCacheControl& rhs);

class CacheHeadersUtils {
public:
  // Parses header_entry as an HTTP time. Returns SystemTime() if
  // header_entry is null or malformed.
  static SystemTime httpTime(const Http::HeaderEntry* header_entry);
};
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
