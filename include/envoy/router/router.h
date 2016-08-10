#pragma once

#include "envoy/common/optional.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"

namespace Router {

/**
 * A routing primitive that creates a redirect path.
 */
class RedirectEntry {
public:
  virtual ~RedirectEntry() {}

  /**
   * Returns the redirect path based on the request headers.
   * @param headers supplies the request headers.
   * @return std::string the redirect URL.
   */
  virtual std::string newPath(const Http::HeaderMap& headers) const PURE;
};

/**
 * Route level retry policy.
 */
class RetryPolicy {
public:
  // clang-format off
  static const uint32_t RETRY_ON_5XX             = 0x1;
  static const uint32_t RETRY_ON_CONNECT_FAILURE = 0x2;
  static const uint32_t RETRY_ON_RETRIABLE_4XX   = 0x4;
  // clang-format on

  virtual ~RetryPolicy() {}

  /**
   * @return uint32_t the number of retries to allow against the route.
   */
  virtual uint32_t numRetries() const PURE;

  /**
   * @return uint32_t a local OR of RETRY_ON values above.
   */
  virtual uint32_t retryOn() const PURE;
};

/**
 * Wraps retry state for an active routed request.
 */
class RetryState {
public:
  typedef std::function<void()> DoRetryCallback;

  virtual ~RetryState() {}

  /**
   * @return true if a policy is in place for the active request that allows retries.
   */
  virtual bool enabled() PURE;

  /**
   * Determine whether a request should be retried based on the response.
   * @param response_headers supplies the response headers if available.
   * @param reset_reason supplies the reset reason if available.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   *                 inline.
   * @return TRUE if a retry should take place. @param callback will be called at some point in the
   *         future. Otherwise a retry should not take place and the callback will never be called.
   *         Calling code should proceed with error handling.
   */
  virtual bool shouldRetry(const Http::HeaderMap* response_headers,
                           const Optional<Http::StreamResetReason>& reset_reason,
                           DoRetryCallback callback) PURE;
};

typedef std::unique_ptr<RetryState> RetryStatePtr;

/**
 * Per route policy for rate limiting.
 */
class RateLimitPolicy {
public:
  virtual ~RateLimitPolicy() {}

  /**
   * @return whether the global rate limiting service should be called for the owning route.
   */
  virtual bool doGlobalLimiting() const PURE;
};

/**
 * An individual resolved route entry.
 */
class RouteEntry {
public:
  virtual ~RouteEntry() {}

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * Do potentially destructive header transforms on request headers prior to forwarding. For
   * example URL prefix rewriting, adding headers, etc. This should only be called ONCE
   * immediately prior to forwarding. It is done this way vs. copying for performance reasons.
   * @param headers supplise the request headers, which may be modified during this call.
   */
  virtual void finalizeRequestHeaders(Http::HeaderMap& headers) const PURE;

  /**
   * @return const RateLimitPolicy& the rate limit policy for the route.
   */
  virtual const RateLimitPolicy& rateLimitPolicy() const PURE;

  /**
   * @return const RetryPolicy& the retry policy for the route. All routes have a retry policy even
   *         if it is empty and does not allow retries.
   */
  virtual const RetryPolicy& retryPolicy() const PURE;

  /**
   * @return std::chrono::milliseconds the route's timeout.
   */
  virtual std::chrono::milliseconds timeout() const PURE;

  /**
   * Determine whether a specific request path belongs to a virtual cluster for use in stats, etc.
   * @param headers supplies the request headers.
   * @return the virtual cluster or empty string if there is no match.
   */
  virtual const std::string& virtualClusterName(const Http::HeaderMap& headers) const PURE;

  /**
   * @return const std::string& the virtual host that owns the route.
   */
  virtual const std::string& virtualHostName() const PURE;
};

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() {}

  /**
   * Based on the incoming HTTP request headers, determine whether a redirect should take place.
   * @param headers supplies the request headers.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return the redirect entry or nullptr if there is no redirect needed for the request.
   */
  virtual const RedirectEntry* redirectRequest(const Http::HeaderMap& headers,
                                               uint64_t random_value) const PURE;

  /**
   * Based on the incoming HTTP request headers, choose the target route to send the remainder
   * of the request to.
   * @param headers supplies the request headers.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeForRequest(const Http::HeaderMap& headers,
                                            uint64_t random_value) const PURE;

  /**
   * Return a list of headers that will be cleaned from any requests that are not from an internal
   * (RFC1918) source.
   */
  virtual const std::list<Http::LowerCaseString>& internalOnlyHeaders() const PURE;

  /**
   * Return a list of header key/value pairs that will be added to every response that transits the
   * router.
   */
  virtual const std::list<std::pair<Http::LowerCaseString, std::string>>&
  responseHeadersToAdd() const PURE;

  /**
   * Return a list of upstream headers that will be stripped from every response that transits the
   * router.
   */
  virtual const std::list<Http::LowerCaseString>& responseHeadersToRemove() const PURE;
};

typedef std::unique_ptr<Config> ConfigPtr;

/**
 * An interface into the config that removes the random value parameters. An implementation is
 * expected to pass the same random_value for all wrapped calls.
 */
class StableRouteTable {
public:
  virtual ~StableRouteTable() {}

  /**
   * Based on the incoming HTTP request headers, determine whether a redirect should take place.
   * @param headers supplies the request headers.
   * @return the redirect entry or nullptr if there is no redirect needed for the request.
   */
  virtual const RedirectEntry* redirectRequest(const Http::HeaderMap& headers) const PURE;

  /**
   * Based on the incoming HTTP request headers, choose the target route to send the remainder
   * of the request to.
   * @param headers supplies the request headers.
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeForRequest(const Http::HeaderMap& headers) const PURE;
};

} // Router
