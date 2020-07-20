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

// According to: https://httpwg.org/specs/rfc7234.html#cache-request-directive
struct RequestCacheControl {
  RequestCacheControl() = default;
  explicit RequestCacheControl(absl::string_view cache_control_header);

  // must_validate is true if 'no-cache' directive is present
  // A cached response must not be served without successful validation with the origin
  bool must_validate_ = false;

  // The response to this request must not be cached (stored)
  bool no_store_ = false;

  // 'no-transform' directive is not used now
  // No transformations should be done to the response of this request, as defined by:
  // https://httpwg.org/specs/rfc7230.html#message.transformations
  bool no_transform_ = false;

  // 'only-if-cached' directive is not used now
  // The request should be satisfied using a cached response, or respond with 504 (Gateway Error)
  bool only_if_cached_ = false;

  // The client is unwilling to receive a cached response whose age exceeds the max-age
  OptionalDuration max_age_;

  // The client is unwilling to received a cached response that satisfies:
  //   expiration_time - now < min-fresh
  OptionalDuration min_fresh_;

  // The client is willing to receive a stale response that satisfies:
  //   now - expiration_time < max-stale
  // If max-stale has no value then the client is willing to receive any stale response
  OptionalDuration max_stale_;
};

// According to: https://httpwg.org/specs/rfc7234.html#cache-response-directive
struct ResponseCacheControl {
  ResponseCacheControl() = default;
  explicit ResponseCacheControl(absl::string_view cache_control_header);

  // must_validate is true if 'no-cache' directive is present; arguments are ignored for now
  // This response must not be used to satisfy subsequent requests without successful validation
  // with the origin
  bool must_validate_ = false;

  // no_store is true if any of 'no-store' or 'private' directives is present.
  // 'private' arguments are ignored for now so it is equivalent to 'no-store'
  // This response must not be cached (stored)
  bool no_store_ = false;

  // 'no-transform' directive is not used now
  // No transformations should be done to this response , as defined by:
  // https://httpwg.org/specs/rfc7230.html#message.transformations
  bool no_transform_ = false;

  // no_stale is true if any of 'must-revalidate' or 'proxy-revalidate' directives is present
  // This response must not be served stale without successful validation with the origin
  bool no_stale_ = false;

  // 'public' directive is not used now
  // This response may be stored, even if the response would normally be non-cacheable or cacheable
  // only within a private cache, see:
  // https://httpwg.org/specs/rfc7234.html#cache-response-directive.public
  bool is_public_ = false;

  // max_age is set if to 's-maxage' if present, if not it is set to 'max-age' if present.
  // Indicates the maximum time after which this response will be considered stale
  OptionalDuration max_age_;
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
