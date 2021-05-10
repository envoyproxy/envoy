#pragma once

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/time.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

/**
 * Tracks alternate protocols that can be used to make an HTTP connection to an origin server.
 * See https://tools.ietf.org/html/rfc7838 for HTTP Alternate Services and
 * https://datatracker.ietf.org/doc/html/draft-ietf-dnsop-svcb-https-04 for the
 * "HTTPS" DNS resource record.
 */
class AlternateProtocolsCache {
public:
  /**
   * Represents an HTTP origin to be connected too.
   */
  struct Origin {
  public:
    Origin(absl::string_view scheme, absl::string_view hostname, uint32_t port);

    bool operator==(const Origin& other) const {
      return std::tie(scheme_, hostname_, port_) ==
             std::tie(other.scheme_, other.hostname_, other.port_);
    }

    bool operator!=(const Origin& other) const { return !this->operator==(other); }

    bool operator<(const Origin& other) const {
      return std::tie(scheme_, hostname_, port_) <
             std::tie(other.scheme_, other.hostname_, other.port_);
    }

    bool operator>(const Origin& other) const {
      return std::tie(scheme_, hostname_, port_) >
             std::tie(other.scheme_, other.hostname_, other.port_);
    }

    bool operator<=(const Origin& other) const {
      return std::tie(scheme_, hostname_, port_) <=
             std::tie(other.scheme_, other.hostname_, other.port_);
    }

    bool operator>=(const Origin& other) const {
      return std::tie(scheme_, hostname_, port_) >=
             std::tie(other.scheme_, other.hostname_, other.port_);
    }

    std::string scheme_;
    std::string hostname_;
    uint32_t port_{};
  };

  /**
   * Represents an alternative protocol that can be used to connect to an origin.
   */
  struct AlternateProtocol {
  public:
    AlternateProtocol(absl::string_view alpn, absl::string_view hostname, uint32_t port);

    bool operator==(const AlternateProtocol& other) const {
      return std::tie(alpn_, hostname_, port_) ==
             std::tie(other.alpn_, other.hostname_, other.port_);
    }

    bool operator!=(const AlternateProtocol& other) const { return !this->operator==(other); }

    std::string alpn_;
    std::string hostname_;
    uint32_t port_;
  };

  virtual ~AlternateProtocolsCache() = default;

  /**
   * Sets the possible alternative protocols which can be used to connect to the
   * specified origin. Expires after the specified expiration time.
   * @param origin The origin to set alternate protocols for.
   * @param protocols A list of alternate protocols.
   * @param expiration The time after which the alternatives are no longer valid.
   */
  virtual void setAlternatives(const Origin& origin,
                               const std::vector<AlternateProtocol>& protocols,
                               const MonotonicTime& expiration) PURE;

  /**
   * Returns the possible alternative protocols which can be used to connect to the
   * specified origin, or nullptr if not alternatives are found. The returned pointer
   * is owned by the AlternateProtocolsCacheImpl and is valid until the next operation on
   * AlternateProtocolsCacheImpl.
   * @param origin The origin to find alternate protocols for.
   * @return An optional list of alternate protocols for the given origin.
   */
  virtual OptRef<const std::vector<AlternateProtocol>> findAlternatives(const Origin& origin) PURE;

  /**
   * Returns the number of entries in the map.
   * @return the number if entries in the map.
   */
  virtual size_t size() const PURE;
};

} // namespace Http
} // namespace Envoy
