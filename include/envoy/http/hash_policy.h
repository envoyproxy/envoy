#pragma once

#include "envoy/http/header_map.h"
#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Http {

class Hashable {
public:
  virtual absl::optional<uint64_t> hash() const PURE;
  virtual ~Hashable() = default;
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
      const std::string& key, const std::string& path, std::chrono::seconds ttl)>;

  /**
   * @param downstream_address is the address of the connected client host, or nullptr if the
   * request is initiated from within this host
   * @param headers stores the HTTP headers for the stream
   * @param add_cookie is called to add a set-cookie header on the reply sent to the downstream
   * host
   * @return absl::optional<uint64_t> an optional hash value to route on. A hash value might not be
   * returned if for example the specified HTTP header does not exist.
   */
  virtual absl::optional<uint64_t>
  generateHash(const Network::Address::Instance* downstream_address,
               const RequestHeaderMap& headers, AddCookieCallback add_cookie,
               const StreamInfo::FilterStateSharedPtr filter_state) const PURE;
};

} // namespace Http
} // namespace Envoy
