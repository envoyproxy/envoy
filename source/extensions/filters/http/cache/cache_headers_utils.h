#pragma once

#include <iosfwd>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/common/matchers.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/btree_set.h"
#include "absl/strings/str_join.h"
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

bool operator==(const RequestCacheControl& lhs, const RequestCacheControl& rhs);
bool operator==(const ResponseCacheControl& lhs, const ResponseCacheControl& rhs);
std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control);
std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control);

namespace CacheHeadersUtils {
// Parses header_entry as an HTTP time. Returns SystemTime() if
// header_entry is null or malformed.
SystemTime httpTime(const Http::HeaderEntry* header_entry);

// Calculates the age of a cached response
Seconds calculateAge(const Http::ResponseHeaderMap& response_headers, SystemTime response_time,
                     SystemTime now);

/**
 * Read a leading positive decimal integer value and advance "*str" past the
 * digits read. If overflow occurs, or no digits exist, return
 * absl::nullopt without advancing "*str".
 */
absl::optional<uint64_t> readAndRemoveLeadingDigits(absl::string_view& str);

// Add to out all header names from the given map that match any of the given rules.
void getAllMatchingHeaderNames(const Http::HeaderMap& headers,
                               const std::vector<Matchers::StringMatcherPtr>& ruleset,
                               absl::flat_hash_set<absl::string_view>& out);

// Parses the values of a comma-delimited list as defined per
// https://tools.ietf.org/html/rfc7230#section-7.
std::vector<absl::string_view> parseCommaDelimitedHeader(const Http::HeaderMap::GetResult& entry);
} // namespace CacheHeadersUtils

class VaryAllowList {
public:
  // Parses the allow list from the Cache Config into the object's private allow_list_.
  VaryAllowList(
      const Protobuf::RepeatedPtrField<envoy::type::matcher::v3::StringMatcher>& allow_list,
      Server::Configuration::CommonFactoryContext& context);

  // Checks if the headers contain an allowed value in the Vary header.
  bool allowsHeaders(const Http::ResponseHeaderMap& headers) const;

  // Checks if this vary header value is allowed to vary cache entries.
  bool allowsValue(const absl::string_view header) const;

private:
  // Stores the matching rules that define whether a header is allowed to be varied.
  std::vector<Matchers::StringMatcherPtr> allow_list_;
};

namespace VaryHeaderUtils {
// Checks if the headers contain a non-empty value in the Vary header.
bool hasVary(const Http::ResponseHeaderMap& headers);

// Retrieve all the individual header values from the provided response header
// map across all vary header entries.
absl::btree_set<absl::string_view> getVaryValues(const Envoy::Http::ResponseHeaderMap& headers);

// Creates a single string combining the values of the varied headers from
// entry_headers. Returns an absl::nullopt if no valid vary key can be created
// and the response should not be cached (eg. when disallowed vary headers are
// present in the response).
absl::optional<std::string>
createVaryIdentifier(const VaryAllowList& allow_list,
                     const absl::btree_set<absl::string_view>& vary_header_values,
                     const Envoy::Http::RequestHeaderMap& request_headers);
} // namespace VaryHeaderUtils

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
