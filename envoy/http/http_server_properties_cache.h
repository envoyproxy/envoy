#pragma once

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/time.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/event/dispatcher.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

/**
 * Tracks information used to make an HTTP connection to an origin server.
 *
 * This includes Alternate protocols, SRTT, Quic brokenness, etc.
 *
 * See https://tools.ietf.org/html/rfc7838 for HTTP Alternative Services and
 * https://datatracker.ietf.org/doc/html/draft-ietf-dnsop-svcb-https-04 for the
 * "HTTPS" DNS resource record.
 */
class HttpServerPropertiesCache {
public:
  /**
   * Represents an HTTP origin to be connected too.
   */
  struct Origin {
  public:
    Origin() = default;
    Origin(const Origin& other) = default;
    Origin(absl::string_view scheme, absl::string_view hostname, uint32_t port)
        : scheme_(scheme), hostname_(hostname), port_(port) {}
    Origin& operator=(const Origin&) = default;

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
   * Represents an alternative protocol that can be used to connect to an origin
   * with a specified expiration time.
   */
  struct AlternateProtocol {
  public:
    AlternateProtocol(absl::string_view alpn, absl::string_view hostname, uint32_t port,
                      MonotonicTime expiration)
        : alpn_(alpn), hostname_(hostname), port_(port), expiration_(expiration) {}

    bool operator==(const AlternateProtocol& other) const {
      return std::tie(alpn_, hostname_, port_, expiration_) ==
             std::tie(other.alpn_, other.hostname_, other.port_, other.expiration_);
    }

    bool operator!=(const AlternateProtocol& other) const { return !this->operator==(other); }

    std::string alpn_;
    std::string hostname_;
    uint32_t port_;
    MonotonicTime expiration_;
  };

  class Http3StatusTracker {
  public:
    virtual ~Http3StatusTracker() = default;

    // Returns true if HTTP/3 is broken.
    virtual bool isHttp3Broken() const PURE;
    // Returns true if HTTP/3 is confirmed to be working.
    virtual bool isHttp3Confirmed() const PURE;
    // Returns true if HTTP/3 has failed recently.
    virtual bool hasHttp3FailedRecently() const PURE;
    // Marks HTTP/3 broken for a period of time, subject to backoff.
    virtual void markHttp3Broken() PURE;
    // Marks HTTP/3 as confirmed to be working and resets the backoff timeout.
    virtual void markHttp3Confirmed() PURE;
    // Marks HTTP/3 as failed recently.
    virtual void markHttp3FailedRecently() PURE;
  };

  virtual ~HttpServerPropertiesCache() = default;

  /**
   * Sets the possible alternative protocols which can be used to connect to the
   * specified origin. Expires after the specified expiration time.
   * @param origin The origin to set alternate protocols for.
   * @param protocols A list of alternate protocols. This list may be truncated
   * by the cache.
   */
  virtual void setAlternatives(const Origin& origin,
                               std::vector<AlternateProtocol>& protocols) PURE;

  /**
   * Sets the srtt estimate for an origin.
   * @param origin The origin to set network characteristics for.
   * @param srtt The smothed round trip time for the origin.
   */
  virtual void setSrtt(const Origin& origin, std::chrono::microseconds srtt) PURE;

  /**
   * Returns the srtt estimate for an origin, or zero, if no srtt is cached.
   * @param origin The origin to get network characteristics for.
   */
  virtual std::chrono::microseconds getSrtt(const Origin& origin) const PURE;

  /**
   * Sets the number of concurrent streams allowed by the last connection to this origin.
   * @param origin The origin to set network characteristics for.
   * @param srtt The number of concurrent streams allowed.
   */
  virtual void setConcurrentStreams(const Origin& origin, uint32_t concurrent_streams) PURE;

  /**
   * Returns the number of concurrent streams allowed by the last connection to this origin,
   * or zero if no limit was set.
   * Note that different servers serving a given origin may have different
   * characteristics, so this is a best guess estimate not a guarantee.
   * @param origin The origin to get network characteristics for.
   */
  virtual uint32_t getConcurrentStreams(const Origin& origin) const PURE;

  /**
   * Returns the possible alternative protocols which can be used to connect to the
   * specified origin, or nullptr if not alternatives are found. The returned reference
   * is owned by the HttpServerPropertiesCache and is valid until the next operation on the
   * HttpServerPropertiesCache.
   * @param origin The origin to find alternate protocols for.
   * @return An optional list of alternate protocols for the given origin.
   */
  virtual OptRef<const std::vector<AlternateProtocol>> findAlternatives(const Origin& origin) PURE;

  /**
   * Returns the number of entries in the map.
   * @return the number if entries in the map.
   */
  virtual size_t size() const PURE;

  /**
   * @param origin The origin to get HTTP/3 status tracker for.
   * @return the existing status tracker or creating a new one if there is none.
   */
  virtual HttpServerPropertiesCache::Http3StatusTracker&
  getOrCreateHttp3StatusTracker(const Origin& origin) PURE;
};

using HttpServerPropertiesCacheSharedPtr = std::shared_ptr<HttpServerPropertiesCache>;
using Http3StatusTrackerPtr = std::unique_ptr<HttpServerPropertiesCache::Http3StatusTracker>;

/**
 * A manager for multiple alternate protocols caches.
 */
class HttpServerPropertiesCacheManager {
public:
  virtual ~HttpServerPropertiesCacheManager() = default;

  /**
   * Get an alternate protocols cache.
   * @param config supplies the cache parameters. If a cache exists with the same parameters it
   *               will be returned, otherwise a new one will be created.
   * @param dispatcher supplies the current thread's dispatcher, for cache creation.
   */
  virtual HttpServerPropertiesCacheSharedPtr
  getCache(const envoy::config::core::v3::AlternateProtocolsCacheOptions& config,
           Event::Dispatcher& dispatcher) PURE;
};

using HttpServerPropertiesCacheManagerSharedPtr = std::shared_ptr<HttpServerPropertiesCacheManager>;

/**
 * Factory for getting an alternate protocols cache manager.
 */
class HttpServerPropertiesCacheManagerFactory {
public:
  virtual ~HttpServerPropertiesCacheManagerFactory() = default;

  /**
   * Get the alternate protocols cache manager.
   */
  virtual HttpServerPropertiesCacheManagerSharedPtr get() PURE;
};

} // namespace Http
} // namespace Envoy
