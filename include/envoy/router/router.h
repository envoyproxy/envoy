#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/retry.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "absl/types/optional.h"

namespace Envoy {

namespace Upstream {
class ClusterManager;
}

namespace Router {

/**
 * Functionality common among routing primitives, such as DirectResponseEntry and RouteEntry.
 */
class ResponseEntry {
public:
  virtual ~ResponseEntry() {}

  /**
   * Do potentially destructive header transforms on response headers prior to forwarding. For
   * example, adding or removing headers. This should only be called ONCE immediately after
   * obtaining the initial response headers.
   * @param headers supplies the response headers, which may be modified during this call.
   * @param stream_info holds additional information about the request.
   */
  virtual void finalizeResponseHeaders(Http::HeaderMap& headers,
                                       const StreamInfo::StreamInfo& stream_info) const PURE;
};

/**
 * A routing primitive that specifies a direct (non-proxied) HTTP response.
 */
class DirectResponseEntry : public ResponseEntry {
public:
  virtual ~DirectResponseEntry() {}

  /**
   * Returns the HTTP status code to return.
   * @return Http::Code the response Code.
   */
  virtual Http::Code responseCode() const PURE;

  /**
   * Returns the redirect path based on the request headers.
   * @param headers supplies the request headers.
   * @return std::string the redirect URL if this DirectResponseEntry is a redirect,
   *         or an empty string otherwise.
   */
  virtual std::string newPath(const Http::HeaderMap& headers) const PURE;

  /**
   * Returns the response body to send with direct responses.
   * @return std::string& the response body specified in the route configuration,
   *         or an empty string if no response body is specified.
   */
  virtual const std::string& responseBody() const PURE;

  /**
   * Do potentially destructive header transforms on Path header prior to redirection. For
   * example prefix rewriting for redirects etc. This should only be called ONCE
   * immediately prior to redirecting.
   * @param headers supplies the request headers, which may be modified during this call.
   * @param insert_envoy_original_path insert x-envoy-original-path header?
   */
  virtual void rewritePathHeader(Http::HeaderMap& headers,
                                 bool insert_envoy_original_path) const PURE;
};

/**
 * CorsPolicy for Route and VirtualHost.
 */
class CorsPolicy {
public:
  virtual ~CorsPolicy() {}

  /**
   * @return std::list<std::string>& access-control-allow-origin values.
   */
  virtual const std::list<std::string>& allowOrigins() const PURE;

  /*
   * @return std::list<std::regex>& regexes that match allowed origins.
   */
  virtual const std::list<std::regex>& allowOriginRegexes() const PURE;

  /**
   * @return std::string access-control-allow-methods value.
   */
  virtual const std::string& allowMethods() const PURE;

  /**
   * @return std::string access-control-allow-headers value.
   */
  virtual const std::string& allowHeaders() const PURE;

  /**
   * @return std::string access-control-expose-headers value.
   */
  virtual const std::string& exposeHeaders() const PURE;

  /**
   * @return std::string access-control-max-age value.
   */
  virtual const std::string& maxAge() const PURE;

  /**
   * @return const absl::optional<bool>& Whether access-control-allow-credentials should be true.
   */
  virtual const absl::optional<bool>& allowCredentials() const PURE;

  /**
   * @return bool Whether CORS is enabled for the route or virtual host.
   */
  virtual bool enabled() const PURE;
};

/**
 * Route level retry policy.
 */
class RetryPolicy {
public:
  // clang-format off
  static const uint32_t RETRY_ON_5XX                     = 0x1;
  static const uint32_t RETRY_ON_GATEWAY_ERROR           = 0x2;
  static const uint32_t RETRY_ON_CONNECT_FAILURE         = 0x4;
  static const uint32_t RETRY_ON_RETRIABLE_4XX           = 0x8;
  static const uint32_t RETRY_ON_REFUSED_STREAM          = 0x10;
  static const uint32_t RETRY_ON_GRPC_CANCELLED          = 0x20;
  static const uint32_t RETRY_ON_GRPC_DEADLINE_EXCEEDED  = 0x40;
  static const uint32_t RETRY_ON_GRPC_RESOURCE_EXHAUSTED = 0x80;
  static const uint32_t RETRY_ON_GRPC_UNAVAILABLE        = 0x100;
  static const uint32_t RETRY_ON_GRPC_INTERNAL           = 0x200;
  static const uint32_t RETRY_ON_RETRIABLE_STATUS_CODES  = 0x400;
  // clang-format on

  virtual ~RetryPolicy() {}

  /**
   * @return std::chrono::milliseconds timeout per retry attempt.
   */
  virtual std::chrono::milliseconds perTryTimeout() const PURE;

  /**
   * @return uint32_t the number of retries to allow against the route.
   */
  virtual uint32_t numRetries() const PURE;

  /**
   * @return uint32_t a local OR of RETRY_ON values above.
   */
  virtual uint32_t retryOn() const PURE;

  /**
   * Initializes a new set of RetryHostPredicates to be used when retrying with this retry policy.
   * @return list of RetryHostPredicates to use
   */
  virtual std::vector<Upstream::RetryHostPredicateSharedPtr> retryHostPredicates() const PURE;

  /**
   * Initializes a RetryPriority to be used when retrying with this retry policy.
   * @return the RetryPriority to use when determining priority load for retries, or nullptr
   * if none should be used.
   */
  virtual Upstream::RetryPrioritySharedPtr retryPriority() const PURE;

  /**
   * Number of times host selection should be reattempted when selecting a host
   * for a retry attempt.
   */
  virtual uint32_t hostSelectionMaxAttempts() const PURE;

  /**
   * List of status codes that should trigger a retry when the retriable-status-codes retry
   * policy is enabled.
   */
  virtual const std::vector<uint32_t>& retriableStatusCodes() const PURE;
};

/**
 * RetryStatus whether request should be retried or not.
 */
enum class RetryStatus { No, NoOverflow, Yes };

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
   * @return RetryStatus if a retry should take place. @param callback will be called at some point
   *         in the future. Otherwise a retry should not take place and the callback will never be
   *         called. Calling code should proceed with error handling.
   */
  virtual RetryStatus shouldRetry(const Http::HeaderMap* response_headers,
                                  const absl::optional<Http::StreamResetReason>& reset_reason,
                                  DoRetryCallback callback) PURE;

  /**
   * Called when a host was attempted but the request failed and is eligible for another retry.
   * Should be used to update whatever internal state depends on previously attempted hosts.
   * @param host the previously attempted host.
   */
  virtual void onHostAttempted(Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * Determine whether host selection should be reattempted. Applies to host selection during
   * retries, and is used to provide configurable host selection for retries.
   * @param host the host under consideration
   * @return whether host selection should be reattempted
   */
  virtual bool shouldSelectAnotherHost(const Upstream::Host& host) PURE;

  /**
   * Returns a reference to the PriorityLoad that should be used for the next retry.
   * @param priority_set current priority set.
   * @param priority_load original priority load.
   * @return PriorityLoad that should be used to select a priority for the next retry.
   */
  virtual const Upstream::PriorityLoad&
  priorityLoadForRetry(const Upstream::PrioritySet& priority_set,
                       const Upstream::PriorityLoad& priority_load) PURE;
  /**
   * return how many times host selection should be reattempted during host selection.
   */
  virtual uint32_t hostSelectionMaxAttempts() const PURE;
};

typedef std::unique_ptr<RetryState> RetryStatePtr;

/**
 * Per route policy for request shadowing.
 */
class ShadowPolicy {
public:
  virtual ~ShadowPolicy() {}

  /**
   * @return the name of the cluster that a matching request should be shadowed to. Returns empty
   *         string if no shadowing should take place.
   */
  virtual const std::string& cluster() const PURE;

  /**
   * @return the runtime key that will be used to determine whether an individual request should
   *         be shadowed. The lack of a key means that all requests will be shadowed. If a key is
   *         present it will be used to drive random selection in the range 0-10000 for 0.01%
   *         increments.
   */
  virtual const std::string& runtimeKey() const PURE;
};

/**
 * Virtual cluster definition (allows splitting a virtual host into virtual clusters orthogonal to
 * routes for stat tracking and priority purposes).
 */
class VirtualCluster {
public:
  virtual ~VirtualCluster() {}

  /**
   * @return the name of the virtual cluster.
   */
  virtual const std::string& name() const PURE;
};

class RateLimitPolicy;
class Config;

/**
 * All route specific config returned by the method at
 *   NamedHttpFilterConfigFactory::createRouteSpecificFilterConfig
 * should be derived from this class.
 */
class RouteSpecificFilterConfig {
public:
  virtual ~RouteSpecificFilterConfig() {}
};
typedef std::shared_ptr<const RouteSpecificFilterConfig> RouteSpecificFilterConfigConstSharedPtr;

/**
 * Virtual host definition.
 */
class VirtualHost {
public:
  virtual ~VirtualHost() {}

  /**
   * @return const CorsPolicy* the CORS policy for this virtual host.
   */
  virtual const CorsPolicy* corsPolicy() const PURE;

  /**
   * @return const std::string& the name of the virtual host.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return const RateLimitPolicy& the rate limit policy for the virtual host.
   */
  virtual const RateLimitPolicy& rateLimitPolicy() const PURE;

  /**
   * @return const Config& the RouteConfiguration that owns this virtual host.
   */
  virtual const Config& routeConfig() const PURE;

  /**
   * @return const RouteSpecificFilterConfig* the per-filter config pre-processed object for
   *  the given filter name. If there is not per-filter config, or the filter factory returns
   *  nullptr, nullptr is returned.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig(const std::string& name) const PURE;

  /**
   * This is a helper on top of perFilterConfig() that casts the return object to the specified
   * type.
   */
  template <class Derived> const Derived* perFilterConfigTyped(const std::string& name) const {
    return dynamic_cast<const Derived*>(perFilterConfig(name));
  }

  /**
   * @return bool whether to include the request count header in upstream requests.
   */
  virtual bool includeAttemptCount() const PURE;
};

/**
 * Route hash policy. I.e., if using a hashing load balancer, how the route should be hashed onto
 * an upstream host.
 */
class HashPolicy {
public:
  virtual ~HashPolicy() {}

  /**
   * A callback used for requesting that a cookie be set with the given lifetime.
   * @param key the name of the cookie to be set
   * @param path the path of the cookie, or the empty string if no path should be set.
   * @param ttl the lifetime of the cookie
   * @return std::string the opaque value of the cookie that will be set
   */
  typedef std::function<std::string(const std::string& key, const std::string& path,
                                    std::chrono::seconds ttl)>
      AddCookieCallback;

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
  generateHash(const Network::Address::Instance* downstream_address, const Http::HeaderMap& headers,
               AddCookieCallback add_cookie) const PURE;
};

class MetadataMatchCriterion {
public:
  virtual ~MetadataMatchCriterion() {}

  /*
   * @return const std::string& the name of the metadata key
   */
  virtual const std::string& name() const PURE;

  /*
   * @return const Envoy::HashedValue& the value for the metadata key
   */
  virtual const HashedValue& value() const PURE;
};

typedef std::shared_ptr<const MetadataMatchCriterion> MetadataMatchCriterionConstSharedPtr;

class MetadataMatchCriteria;
typedef std::unique_ptr<const MetadataMatchCriteria> MetadataMatchCriteriaConstPtr;

class MetadataMatchCriteria {
public:
  virtual ~MetadataMatchCriteria() {}

  /*
   * @return std::vector<MetadataMatchCriterionConstSharedPtr>& a vector of
   * metadata to be matched against upstream endpoints when load
   * balancing, sorted lexically by name.
   */
  virtual const std::vector<MetadataMatchCriterionConstSharedPtr>&
  metadataMatchCriteria() const PURE;

  /**
   * Creates a new MetadataMatchCriteria, merging existing
   * metadata criteria with the provided criteria. The result criteria is the
   * combination of both sets of criteria, with those from the metadata_matches
   * ProtobufWkt::Struct taking precedence.
   * @param metadata_matches supplies the new criteria.
   * @return MetadataMatchCriteriaConstPtr the result criteria.
   */
  virtual MetadataMatchCriteriaConstPtr
  mergeMatchCriteria(const ProtobufWkt::Struct& metadata_matches) const PURE;
};

/**
 * Type of path matching that a route entry uses.
 */
enum class PathMatchType {
  None,
  Prefix,
  Exact,
  Regex,
};

/**
 * Criterion that a route entry uses for matching a particular path.
 */
class PathMatchCriterion {
public:
  virtual ~PathMatchCriterion() {}

  /**
   * @return PathMatchType type of path match.
   */
  virtual PathMatchType matchType() const PURE;

  /**
   * @return const std::string& the string with which to compare paths.
   */
  virtual const std::string& matcher() const PURE;
};

/**
 * Base class for all route typed metadata factories.
 */
class HttpRouteTypedMetadataFactory : public Envoy::Config::TypedMetadataFactory {};

/**
 * An individual resolved route entry.
 */
class RouteEntry : public ResponseEntry {
public:
  virtual ~RouteEntry() {}

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * Returns the HTTP status code to use when configured cluster is not found.
   * @return Http::Code to use when configured cluster is not found.
   */
  virtual Http::Code clusterNotFoundResponseCode() const PURE;

  /**
   * @return const CorsPolicy* the CORS policy for this virtual host.
   */
  virtual const CorsPolicy* corsPolicy() const PURE;

  /**
   * Do potentially destructive header transforms on request headers prior to forwarding. For
   * example URL prefix rewriting, adding headers, etc. This should only be called ONCE
   * immediately prior to forwarding. It is done this way vs. copying for performance reasons.
   * @param headers supplies the request headers, which may be modified during this call.
   * @param stream_info holds additional information about the request.
   * @param insert_envoy_original_path insert x-envoy-original-path header if path rewritten?
   */
  virtual void finalizeRequestHeaders(Http::HeaderMap& headers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      bool insert_envoy_original_path) const PURE;

  /**
   * @return const HashPolicy* the optional hash policy for the route.
   */
  virtual const HashPolicy* hashPolicy() const PURE;

  /**
   * @return the priority of the route.
   */
  virtual Upstream::ResourcePriority priority() const PURE;

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
   * @return const ShadowPolicy& the shadow policy for the route. All routes have a shadow policy
   *         even if no shadowing takes place.
   */
  virtual const ShadowPolicy& shadowPolicy() const PURE;

  /**
   * @return std::chrono::milliseconds the route's timeout.
   */
  virtual std::chrono::milliseconds timeout() const PURE;

  /**
   * @return optional<std::chrono::milliseconds> the route's idle timeout. Zero indicates a
   *         disabled idle timeout, while nullopt indicates deference to the global timeout.
   */
  virtual absl::optional<std::chrono::milliseconds> idleTimeout() const PURE;

  /**
   * @return absl::optional<std::chrono::milliseconds> the maximum allowed timeout value derived
   * from 'grpc-timeout' header of a gRPC request. Non-present value disables use of 'grpc-timeout'
   * header, while 0 represents infinity.
   */
  virtual absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const PURE;

  /**
   * Determine whether a specific request path belongs to a virtual cluster for use in stats, etc.
   * @param headers supplies the request headers.
   * @return the virtual cluster or nullptr if there is no match.
   */
  virtual const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const PURE;

  /**
   * @return const VirtualHost& the virtual host that owns the route.
   */
  virtual const VirtualHost& virtualHost() const PURE;

  /**
   * @return bool true if the :authority header should be overwritten with the upstream hostname.
   */
  virtual bool autoHostRewrite() const PURE;

  /**
   * @return MetadataMatchCriteria* the metadata that a subset load balancer should match when
   * selecting an upstream host
   */
  virtual const MetadataMatchCriteria* metadataMatchCriteria() const PURE;

  /**
   * @return const std::multimap<std::string, std::string> the opaque configuration associated
   *         with the route
   */
  virtual const std::multimap<std::string, std::string>& opaqueConfig() const PURE;

  /**
   * @return bool true if the virtual host rate limits should be included.
   */
  virtual bool includeVirtualHostRateLimits() const PURE;

  /**
   * @return const Envoy::Config::TypedMetadata& return the typed metadata provided in the config
   * for this route.
   */
  virtual const Envoy::Config::TypedMetadata& typedMetadata() const PURE;

  /**
   * @return const envoy::api::v2::core::Metadata& return the metadata provided in the config for
   * this route.
   */
  virtual const envoy::api::v2::core::Metadata& metadata() const PURE;

  /**
   * @return const PathMatchCriterion& the match criterion for this route.
   */
  virtual const PathMatchCriterion& pathMatchCriterion() const PURE;

  /**
   * @return const RouteSpecificFilterConfig* the per-filter config pre-processed object for
   *  the given filter name. If there is not per-filter config, or the filter factory returns
   *  nullptr, nullptr is returned.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig(const std::string& name) const PURE;

  /**
   * This is a helper on top of perFilterConfig() that casts the return object to the specified
   * type.
   */
  template <class Derived> const Derived* perFilterConfigTyped(const std::string& name) const {
    return dynamic_cast<const Derived*>(perFilterConfig(name));
  };

  /**
   * True if the virtual host this RouteEntry belongs to is configured to include the attempt
   * count header.
   * @return bool whether x-envoy-attempt-count should be included on the upstream request.
   */
  virtual bool includeAttemptCount() const PURE;
};

/**
 * An interface representing the Decorator.
 */
class Decorator {
public:
  virtual ~Decorator() {}

  /**
   * This method decorates the supplied span.
   * @param Tracing::Span& the span.
   */
  virtual void apply(Tracing::Span& span) const PURE;

  /**
   * This method returns the operation name.
   * @return the operation name
   */
  virtual const std::string& getOperation() const PURE;
};

typedef std::unique_ptr<const Decorator> DecoratorConstPtr;

/**
 * An interface that holds a DirectResponseEntry or RouteEntry for a request.
 */
class Route {
public:
  virtual ~Route() {}

  /**
   * @return the direct response entry or nullptr if there is no direct response for the request.
   */
  virtual const DirectResponseEntry* directResponseEntry() const PURE;

  /**
   * @return the route entry or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeEntry() const PURE;

  /**
   * @return the decorator or nullptr if not defined for the request.
   */
  virtual const Decorator* decorator() const PURE;

  /**
   * @return const RouteSpecificFilterConfig* the per-filter config pre-processed object for
   *  the given filter name. If there is not per-filter config, or the filter factory returns
   *  nullptr, nullptr is returned.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig(const std::string& name) const PURE;

  /**
   * This is a helper on top of perFilterConfig() that casts the return object to the specified
   * type.
   */
  template <class Derived> const Derived* perFilterConfigTyped(const std::string& name) const {
    return dynamic_cast<const Derived*>(perFilterConfig(name));
  }
};

typedef std::shared_ptr<const Route> RouteConstSharedPtr;

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() {}

  /**
   * Based on the incoming HTTP request headers, determine the target route (containing either a
   * route entry or a direct response entry) for the request.
   * @param headers supplies the request headers.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(const Http::HeaderMap& headers,
                                    uint64_t random_value) const PURE;

  /**
   * Return a list of headers that will be cleaned from any requests that are not from an internal
   * (RFC1918) source.
   */
  virtual const std::list<Http::LowerCaseString>& internalOnlyHeaders() const PURE;

  /**
   * @return const std::string the RouteConfiguration name.
   */
  virtual const std::string& name() const PURE;
};

typedef std::shared_ptr<const Config> ConfigConstSharedPtr;

} // namespace Router
} // namespace Envoy
