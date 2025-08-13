#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Http {

/**
 * CookieAttribute that stores the name and value of a cookie.
 */
class CookieAttribute {
public:
  std::string name_;
  std::string value_;
};

/**
 * Request hash policy. I.e., if using a hashing load balancer, how a request should be hashed onto
 * an upstream host.
 */
class HashPolicy {
public:
  virtual ~HashPolicy() = default;

  /**
   * A callback used for requesting that a cookie be set with the given lifetime.
   * @param key the name of the cookie to be set
   * @param path the path of the cookie, or the empty string if no path should be set.
   * @param ttl the lifetime of the cookie
   * @return std::string the opaque value of the cookie that will be set
   */
  using AddCookieCallback = std::function<std::string(
      absl::string_view name, absl::string_view path, std::chrono::seconds ttl,
      absl::Span<const CookieAttribute> attributes)>;

  /**
   * @param headers stores the HTTP headers for the stream.
   * @param info stores the stream info for the stream.
   * @param add_cookie is called to add a set-cookie header on the reply sent to the downstream
   * host.
   * @return absl::optional<uint64_t> an optional hash value to route on. A hash value might not be
   * returned if for example the specified HTTP header does not exist.
   */
  virtual absl::optional<uint64_t> generateHash(OptRef<const RequestHeaderMap> headers,
                                                OptRef<const StreamInfo::StreamInfo> info,
                                                AddCookieCallback add_cookie = nullptr) const PURE;
};

} // namespace Http
} // namespace Envoy
