#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/common/conn_pool.h"
#include "envoy/common/matchers.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/hash_policy.h"
#include "envoy/http/header_map.h"
#include "envoy/router/internal_redirect.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/retry.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "absl/types/optional.h"

namespace Envoy {

namespace Upstream {
class ClusterManager;
class LoadBalancerContext;
} // namespace Upstream

namespace Router {

/**
 * Functionality common among routing primitives, such as DirectResponseEntry and RouteEntry.
 */
class ResponseEntry {
public:
  virtual ~ResponseEntry() = default;

  /**
   * Do potentially destructive header transforms on response headers prior to forwarding. For
   * example, adding or removing headers. This should only be called ONCE immediately after
   * obtaining the initial response headers.
   * @param headers supplies the response headers, which may be modified during this call.
   * @param stream_info holds additional information about the request.
   */
  virtual void finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                                       const StreamInfo::StreamInfo& stream_info) const PURE;
};

/**
 * A routing primitive that specifies a direct (non-proxied) HTTP response.
 */
class DirectResponseEntry : public ResponseEntry {
public:
  ~DirectResponseEntry() override = default;

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
  virtual std::string newPath(const Http::RequestHeaderMap& headers) const PURE;

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
  virtual void rewritePathHeader(Http::RequestHeaderMap& headers,
                                 bool insert_envoy_original_path) const PURE;

  /**
   * @return std::string& the name of the route.
   */
  virtual const std::string& routeName() const PURE;
};

/**
 * CorsPolicy for Route and VirtualHost.
 */
class CorsPolicy {
public:
  virtual ~CorsPolicy() = default;

  /**
   * @return std::vector<StringMatcherPtr>& access-control-allow-origin matchers.
   */
  virtual const std::vector<Matchers::StringMatcherPtr>& allowOrigins() const PURE;

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

  /**
   * @return bool Whether CORS policies are evaluated when filter is off.
   */
  virtual bool shadowEnabled() const PURE;
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
  static const uint32_t RETRY_ON_RESET                   = 0x800;
  static const uint32_t RETRY_ON_RETRIABLE_HEADERS       = 0x1000;
  static const uint32_t RETRY_ON_ENVOY_RATE_LIMITED      = 0x2000;
  // clang-format on

  virtual ~RetryPolicy() = default;

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

  /**
   * @return std::vector<Http::HeaderMatcherSharedPtr>& list of response header matchers that
   * will be checked when the 'retriable-headers' retry policy is enabled.
   */
  virtual const std::vector<Http::HeaderMatcherSharedPtr>& retriableHeaders() const PURE;

  /**
   * @return std::vector<Http::HeaderMatcherSharedPt>& list of request header
   * matchers that will be checked before enabling retries.
   */
  virtual const std::vector<Http::HeaderMatcherSharedPtr>& retriableRequestHeaders() const PURE;

  /**
   * @return absl::optional<std::chrono::milliseconds> base retry interval
   */
  virtual absl::optional<std::chrono::milliseconds> baseInterval() const PURE;

  /**
   * @return absl::optional<std::chrono::milliseconds> maximum retry interval
   */
  virtual absl::optional<std::chrono::milliseconds> maxInterval() const PURE;

  /**
   * @return std::vector<Http::HeaderMatcherSharedPt>& list of response header
   * matchers that will be attempted to extract a rate limited maximum retry interval.
   */
  virtual const std::vector<Http::HeaderMatcherSharedPtr>& rateLimitedResetHeaders() const PURE;

  /**
   * @return absl::optional<std::chrono::milliseconds> limit placed on a rate limited retry
   * interval.
   */
  virtual std::chrono::milliseconds rateLimitedResetMaxInterval() const PURE;
};

/**
 * RetryStatus whether request should be retried or not.
 */
enum class RetryStatus { No, NoOverflow, NoRetryLimitExceeded, Yes };

/**
 * InternalRedirectPolicy from the route configuration.
 */
class InternalRedirectPolicy {
public:
  virtual ~InternalRedirectPolicy() = default;

  /**
   * @return whether internal redirect is enabled on this route.
   */
  virtual bool enabled() const PURE;

  /**
   * @param response_code the response code from the upstream.
   * @return whether the given response_code should trigger an internal redirect on this route.
   */
  virtual bool shouldRedirectForResponseCode(const Http::Code& response_code) const PURE;

  /**
   * Creates the target route predicates. This should really be called only once for each upstream
   * redirect response. Creating the predicates lazily to avoid wasting CPU cycles on non-redirect
   * responses, which should be the most common case.
   * @return a vector of newly constructed InternalRedirectPredicate instances.
   */
  virtual std::vector<InternalRedirectPredicateSharedPtr> predicates() const PURE;

  /**
   * @return the maximum number of allowed internal redirects on this route.
   */
  virtual uint32_t maxInternalRedirects() const PURE;

  /**
   * @return if it is allowed to follow the redirect with a different scheme in
   *         the target URI than the downstream request.
   */
  virtual bool isCrossSchemeRedirectAllowed() const PURE;
};

/**
 * Wraps retry state for an active routed request.
 */
class RetryState {
public:
  using DoRetryCallback = std::function<void()>;

  virtual ~RetryState() = default;

  /**
   * @return true if a policy is in place for the active request that allows retries.
   */
  virtual bool enabled() PURE;

  /**
   * Attempts to parse any matching rate limited reset headers (RFC 7231), either in the form of an
   * interval directly, or in the form of a unix timestamp relative to the current system time.
   * @return the interval if parsing was successful.
   */
  virtual absl::optional<std::chrono::milliseconds>
  parseRateLimitedResetInterval(const Http::ResponseHeaderMap& response_headers) const PURE;

  /**
   * Determine whether a request should be retried based on the response headers.
   * @param response_headers supplies the response headers.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   *                 inline.
   * @return RetryStatus if a retry should take place. @param callback will be called at some point
   *         in the future. Otherwise a retry should not take place and the callback will never be
   *         called. Calling code should proceed with error handling.
   */
  virtual RetryStatus shouldRetryHeaders(const Http::ResponseHeaderMap& response_headers,
                                         DoRetryCallback callback) PURE;

  /**
   * Determines whether given response headers would be retried by the retry policy, assuming
   * sufficient retry budget and circuit breaker headroom. This is useful in cases where
   * the information about whether a response is "good" or not is useful, but a retry should
   * not be attempted for other reasons.
   * @param response_headers supplies the response headers.
   * @return bool true if a retry would be warranted based on the retry policy.
   */
  virtual bool wouldRetryFromHeaders(const Http::ResponseHeaderMap& response_headers) PURE;

  /**
   * Determine whether a request should be retried after a reset based on the reason for the reset.
   * @param reset_reason supplies the reset reason.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   *                 inline.
   * @return RetryStatus if a retry should take place. @param callback will be called at some point
   *         in the future. Otherwise a retry should not take place and the callback will never be
   *         called. Calling code should proceed with error handling.
   */
  virtual RetryStatus shouldRetryReset(const Http::StreamResetReason reset_reason,
                                       DoRetryCallback callback) PURE;

  /**
   * Determine whether a "hedged" retry should be sent after the per try
   * timeout expires. This means the original request is not canceled, but a
   * new one is sent to hedge against the original request taking even longer.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   *                 inline.
   * @return RetryStatus if a retry should take place. @param callback will be called at some point
   *         in the future. Otherwise a retry should not take place and the callback will never be
   *         called. Calling code should proceed with error handling.
   */
  virtual RetryStatus shouldHedgeRetryPerTryTimeout(DoRetryCallback callback) PURE;

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
   * @param original_priority_load original priority load.
   * @param priority_mapping_func see @Upstream::RetryPriority::PriorityMappingFunc.
   * @return HealthyAndDegradedLoad that should be used to select a priority for the next retry.
   */
  virtual const Upstream::HealthyAndDegradedLoad& priorityLoadForRetry(
      const Upstream::PrioritySet& priority_set,
      const Upstream::HealthyAndDegradedLoad& original_priority_load,
      const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) PURE;

  /**
   * return how many times host selection should be reattempted during host selection.
   */
  virtual uint32_t hostSelectionMaxAttempts() const PURE;

  /**
   * @return the rate limited reset headers used to match against rate limited responses.
   */
  virtual const std::vector<Http::HeaderMatcherSharedPtr>& rateLimitedResetHeaders() const PURE;
};

using RetryStatePtr = std::unique_ptr<RetryState>;

/**
 * Per route policy for request shadowing.
 */
class ShadowPolicy {
public:
  virtual ~ShadowPolicy() = default;

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

  /**
   * @return the default fraction of traffic the should be shadowed, if the runtime key is not
   *         present.
   */
  virtual const envoy::type::v3::FractionalPercent& defaultValue() const PURE;

  /**
   * @return true if the trace span should be sampled.
   */
  virtual bool traceSampled() const PURE;
};

using ShadowPolicyPtr = std::unique_ptr<ShadowPolicy>;

/**
 * All virtual cluster stats. @see stats_macro.h
 */
#define ALL_VIRTUAL_CLUSTER_STATS(COUNTER)                                                         \
  COUNTER(upstream_rq_retry)                                                                       \
  COUNTER(upstream_rq_retry_limit_exceeded)                                                        \
  COUNTER(upstream_rq_retry_overflow)                                                              \
  COUNTER(upstream_rq_retry_success)                                                               \
  COUNTER(upstream_rq_timeout)                                                                     \
  COUNTER(upstream_rq_total)

/**
 * Struct definition for all virtual cluster stats. @see stats_macro.h
 */
struct VirtualClusterStats {
  ALL_VIRTUAL_CLUSTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Virtual cluster definition (allows splitting a virtual host into virtual clusters orthogonal to
 * routes for stat tracking and priority purposes).
 */
class VirtualCluster {
public:
  virtual ~VirtualCluster() = default;

  /**
   * @return the stat-name of the virtual cluster.
   */
  virtual Stats::StatName statName() const PURE;

  /**
   * @return VirtualClusterStats& strongly named stats for this virtual cluster.
   */
  virtual VirtualClusterStats& stats() const PURE;

  static VirtualClusterStats generateStats(Stats::Scope& scope) {
    return {ALL_VIRTUAL_CLUSTER_STATS(POOL_COUNTER(scope))};
  }
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
  virtual ~RouteSpecificFilterConfig() = default;
};
using RouteSpecificFilterConfigConstSharedPtr = std::shared_ptr<const RouteSpecificFilterConfig>;

/**
 * Virtual host definition.
 */
class VirtualHost {
public:
  virtual ~VirtualHost() = default;

  /**
   * @return const CorsPolicy* the CORS policy for this virtual host.
   */
  virtual const CorsPolicy* corsPolicy() const PURE;

  /**
   * @return the stat-name of the virtual host.
   */
  virtual Stats::StatName statName() const PURE;

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
  virtual bool includeAttemptCountInRequest() const PURE;

  /**
   * @return bool whether to include the request count header in the downstream response.
   */
  virtual bool includeAttemptCountInResponse() const PURE;

  /**
   * @return uint32_t any route cap on bytes which should be buffered for shadowing or retries.
   *         This is an upper bound so does not necessarily reflect the bytes which will be buffered
   *         as other limits may apply.
   *         If a per route limit exists, it takes precedence over this configuration.
   *         Unlike some other buffer limits, 0 here indicates buffering should not be performed
   *         rather than no limit applies.
   */
  virtual uint32_t retryShadowBufferLimit() const PURE;
};

/**
 * Route level hedging policy.
 */
class HedgePolicy {
public:
  virtual ~HedgePolicy() = default;

  /**
   * @return number of upstream requests that should be sent initially.
   */
  virtual uint32_t initialRequests() const PURE;

  /**
   * @return percent chance that an additional upstream request should be sent
   * on top of the value from initialRequests().
   */
  virtual const envoy::type::v3::FractionalPercent& additionalRequestChance() const PURE;

  /**
   * @return bool indicating whether request hedging should occur when a request
   * is retried due to a per try timeout. The alternative is the original request
   * will be canceled immediately.
   */
  virtual bool hedgeOnPerTryTimeout() const PURE;
};

class MetadataMatchCriterion {
public:
  virtual ~MetadataMatchCriterion() = default;

  /*
   * @return const std::string& the name of the metadata key
   */
  virtual const std::string& name() const PURE;

  /*
   * @return const Envoy::HashedValue& the value for the metadata key
   */
  virtual const HashedValue& value() const PURE;
};

using MetadataMatchCriterionConstSharedPtr = std::shared_ptr<const MetadataMatchCriterion>;

class MetadataMatchCriteria;
using MetadataMatchCriteriaConstPtr = std::unique_ptr<const MetadataMatchCriteria>;

class MetadataMatchCriteria {
public:
  virtual ~MetadataMatchCriteria() = default;

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

  /**
   * Creates a new MetadataMatchCriteria with criteria vector reduced to given names
   * @param names names of metadata keys to preserve
   * @return MetadataMatchCriteriaConstPtr the result criteria. Returns nullptr if the result
   * criteria are empty.
   */
  virtual MetadataMatchCriteriaConstPtr
  filterMatchCriteria(const std::set<std::string>& names) const PURE;
};

/**
 * Criterion that a route entry uses for matching TLS connection context.
 */
class TlsContextMatchCriteria {
public:
  virtual ~TlsContextMatchCriteria() = default;

  /**
   * @return bool indicating whether the client presented credentials.
   */
  virtual const absl::optional<bool>& presented() const PURE;

  /**
   * @return bool indicating whether the client credentials successfully validated against the TLS
   * context validation context.
   */
  virtual const absl::optional<bool>& validated() const PURE;
};

using TlsContextMatchCriteriaConstPtr = std::unique_ptr<const TlsContextMatchCriteria>;

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
  virtual ~PathMatchCriterion() = default;

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
  ~RouteEntry() override = default;

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
  virtual void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      bool insert_envoy_original_path) const PURE;

  /**
   * @return const HashPolicy* the optional hash policy for the route.
   */
  virtual const Http::HashPolicy* hashPolicy() const PURE;

  /**
   * @return const HedgePolicy& the hedge policy for the route. All routes have a hedge policy even
   *         if it is empty and does not allow for hedged requests.
   */
  virtual const HedgePolicy& hedgePolicy() const PURE;

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
   * @return const InternalRedirectPolicy& the internal redirect policy for the route. All routes
   *         have a internal redirect policy even if it is not enabled, which means redirects are
   *         simply proxied as normal responses.
   */
  virtual const InternalRedirectPolicy& internalRedirectPolicy() const PURE;

  /**
   * @return uint32_t any route cap on bytes which should be buffered for shadowing or retries.
   *         This is an upper bound so does not necessarily reflect the bytes which will be buffered
   *         as other limits may apply.
   *         Unlike some other buffer limits, 0 here indicates buffering should not be performed
   *         rather than no limit applies.
   */
  virtual uint32_t retryShadowBufferLimit() const PURE;

  /**
   * @return const std::vector<ShadowPolicy>& the shadow policies for the route. The vector is empty
   *         if no shadowing takes place.
   */
  virtual const std::vector<ShadowPolicyPtr>& shadowPolicies() const PURE;

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
   * @return absl::optional<std::chrono::milliseconds> the timeout offset to apply to the timeout
   * provided by the 'grpc-timeout' header of a gRPC request. This value will be positive and should
   * be subtracted from the value provided by the header.
   */
  virtual absl::optional<std::chrono::milliseconds> grpcTimeoutOffset() const PURE;

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
   * @return const envoy::config::core::v3::Metadata& return the metadata provided in the config for
   * this route.
   */
  virtual const envoy::config::core::v3::Metadata& metadata() const PURE;

  /**
   * @return TlsContextMatchCriteria* the tls context match criterion for this route. If there is no
   * tls context match criteria, nullptr is returned.
   */
  virtual const TlsContextMatchCriteria* tlsContextMatchCriteria() const PURE;

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
   * This is a helper to get the route's per-filter config if it exists, otherwise the virtual
   * host's. Or nullptr if none of them exist.
   */
  template <class Derived>
  const Derived* mostSpecificPerFilterConfigTyped(const std::string& name) const {
    const Derived* config = perFilterConfigTyped<Derived>(name);
    return config ? config : virtualHost().perFilterConfigTyped<Derived>(name);
  }

  /**
   * True if the virtual host this RouteEntry belongs to is configured to include the attempt
   * count header.
   * @return bool whether x-envoy-attempt-count should be included on the upstream request.
   */
  virtual bool includeAttemptCountInRequest() const PURE;

  /**
   * True if the virtual host this RouteEntry belongs to is configured to include the attempt
   * count header.
   * @return bool whether x-envoy-attempt-count should be included on the downstream response.
   */
  virtual bool includeAttemptCountInResponse() const PURE;

  using UpgradeMap = std::map<std::string, bool>;
  /**
   * @return a map of route-specific upgrades to their enabled/disabled status.
   */
  virtual const UpgradeMap& upgradeMap() const PURE;

  using ConnectConfig = envoy::config::route::v3::RouteAction::UpgradeConfig::ConnectConfig;
  /**
   * If present, informs how to handle proxying CONNECT requests on this route.
   */
  virtual const absl::optional<ConnectConfig>& connectConfig() const PURE;

  /**
   * @return std::string& the name of the route.
   */
  virtual const std::string& routeName() const PURE;
};

/**
 * An interface representing the Decorator.
 */
class Decorator {
public:
  virtual ~Decorator() = default;

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

  /**
   * This method returns whether the decorator information
   * should be propagated to other services.
   * @return whether to propagate
   */
  virtual bool propagate() const PURE;
};

using DecoratorConstPtr = std::unique_ptr<const Decorator>;

/**
 * An interface representing the Tracing for the route configuration.
 */
class RouteTracing {
public:
  virtual ~RouteTracing() = default;

  /**
   * This method returns the client sampling percentage.
   * @return the client sampling percentage
   */
  virtual const envoy::type::v3::FractionalPercent& getClientSampling() const PURE;

  /**
   * This method returns the random sampling percentage.
   * @return the random sampling percentage
   */
  virtual const envoy::type::v3::FractionalPercent& getRandomSampling() const PURE;

  /**
   * This method returns the overall sampling percentage.
   * @return the overall sampling percentage
   */
  virtual const envoy::type::v3::FractionalPercent& getOverallSampling() const PURE;

  /**
   * This method returns the route level tracing custom tags.
   * @return the tracing custom tags.
   */
  virtual const Tracing::CustomTagMap& getCustomTags() const PURE;
};

using RouteTracingConstPtr = std::unique_ptr<const RouteTracing>;

/**
 * An interface that holds a DirectResponseEntry or RouteEntry for a request.
 */
class Route {
public:
  virtual ~Route() = default;

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
   * @return the tracing config or nullptr if not defined for the request.
   */
  virtual const RouteTracing* tracingConfig() const PURE;

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

using RouteConstSharedPtr = std::shared_ptr<const Route>;

/**
 * RouteCallback, returns one of these enums to the route matcher to indicate
 * if the matched route has been accepted or it wants the route matching to
 * continue.
 */
enum class RouteMatchStatus {
  // Continue matching route
  Continue,
  // Accept matched route
  Accept
};

/**
 * RouteCallback is passed this enum to indicate if more routes are available for evaluation.
 */
enum class RouteEvalStatus {
  // Has more routes that can be evaluated for match.
  HasMoreRoutes,
  // All routes have been evaluated for match.
  NoMoreRoutes
};

/**
 * RouteCallback can be used to override routing decision made by the Route::Config::route,
 * this callback is passed the RouteConstSharedPtr, when a matching route is found, and
 * RouteEvalStatus indicating whether there are more routes available for evaluation.
 *
 * RouteCallback will be called back only when at least one matching route is found, if no matching
 * routes are found RouteCallback will not be invoked. RouteCallback can return one of the
 * RouteMatchStatus enum to indicate if the match has been accepted or should the route match
 * evaluation continue.
 *
 * Returning RouteMatchStatus::Continue, when no more routes available for evaluation will result in
 * no further callbacks and no route is deemed to be accepted and nullptr is returned to the caller
 * of Route::Config::route.
 */
using RouteCallback = std::function<RouteMatchStatus(RouteConstSharedPtr, RouteEvalStatus)>;

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * Based on the incoming HTTP request headers, determine the target route (containing either a
   * route entry or a direct response entry) for the request.
   * @param headers supplies the request headers.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(const Http::RequestHeaderMap& headers,
                                    const StreamInfo::StreamInfo& stream_info,
                                    uint64_t random_value) const PURE;

  /**
   * Based on the incoming HTTP request headers, determine the target route (containing either a
   * route entry or a direct response entry) for the request.
   *
   * Invokes callback with matched route, callback can choose to accept the route by returning
   * RouteStatus::Stop or continue route match from last matched route by returning
   * RouteMatchStatus::Continue, when more routes are available.
   *
   * @param cb supplies callback to be invoked upon route match.
   * @param headers supplies the request headers.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return the route accepted by the callback or nullptr if no match found or none of route is
   * accepted by the callback.
   */
  virtual RouteConstSharedPtr route(const RouteCallback& cb, const Http::RequestHeaderMap& headers,
                                    const StreamInfo::StreamInfo& stream_info,
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

  /**
   * @return whether router configuration uses VHDS.
   */
  virtual bool usesVhds() const PURE;

  /**
   * @return bool whether most specific header mutations should take precedence. The default
   * evaluation order is route level, then virtual host level and finally global connection
   * manager level.
   */
  virtual bool mostSpecificHeaderMutationsWins() const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class GenericConnectionPoolCallbacks;
class GenericUpstream;

/**
 * An API for wrapping either an HTTP or a TCP connection pool.
 *
 * The GenericConnPool exists to create a GenericUpstream handle via a call to
 * newStream resulting in an eventual call to onPoolReady
 */
class GenericConnPool {
public:
  virtual ~GenericConnPool() = default;

  /**
   * Called to create a new HTTP stream or TCP connection for "CONNECT streams".
   *
   * The implementation of the GenericConnPool will either call
   * GenericConnectionPoolCallbacks::onPoolReady
   * when a stream is available or GenericConnectionPoolCallbacks::onPoolFailure
   * if stream creation fails.
   *
   * The caller is responsible for calling cancelAnyPendingRequest() if stream
   * creation is no longer desired. newStream may only be called once per
   * GenericConnPool.
   *
   * @param callbacks callbacks to communicate stream failure or creation on.
   */
  virtual void newStream(GenericConnectionPoolCallbacks* callbacks) PURE;
  /**
   * Called to cancel any pending newStream request,
   */
  virtual bool cancelAnyPendingRequest() PURE;
  /**
   * @return optionally returns the protocol for the connection pool.
   */
  virtual absl::optional<Http::Protocol> protocol() const PURE;
  /**
   * @return optionally returns the host for the connection pool.
   */
  virtual Upstream::HostDescriptionConstSharedPtr host() const PURE;
};

/**
 * An API for the interactions the upstream stream needs to have with the downstream stream
 * and/or router components
 */
class UpstreamToDownstream : public Http::ResponseDecoder, public Http::StreamCallbacks {
public:
  /**
   * @return return the routeEntry for the downstream stream.
   */
  virtual const RouteEntry& routeEntry() const PURE;
  /**
   * @return return the connection for the downstream stream.
   */
  virtual const Network::Connection& connection() const PURE;
};

/**
 * An API for wrapping callbacks from either an HTTP or a TCP connection pool.
 *
 * Just like the connection pool callbacks, the GenericConnectionPoolCallbacks
 * will either call onPoolReady when a GenericUpstream is ready, or
 * onPoolFailure if a connection/stream can not be established.
 */
class GenericConnectionPoolCallbacks {
public:
  virtual ~GenericConnectionPoolCallbacks() = default;

  /**
   * Called to indicate a failure for GenericConnPool::newStream to establish a stream.
   *
   * @param reason supplies the failure reason.
   * @param transport_failure_reason supplies the details of the transport failure reason.
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                             absl::string_view transport_failure_reason,
                             Upstream::HostDescriptionConstSharedPtr host) PURE;
  /**
   * Called when GenericConnPool::newStream has established a new stream.
   *
   * @param upstream supplies the generic upstream for the stream.
   * @param host supplies the description of the host that will carry the request. For logical
   *             connection pools the description may be different each time this is called.
   * @param upstream_local_address supplies the local address of the upstream connection.
   * @param info supplies the stream info object associated with the upstream connection.
   */
  virtual void onPoolReady(std::unique_ptr<GenericUpstream>&& upstream,
                           Upstream::HostDescriptionConstSharedPtr host,
                           const Network::Address::InstanceConstSharedPtr& upstream_local_address,
                           const StreamInfo::StreamInfo& info) PURE;

  // @return the UpstreamToDownstream interface for this stream.
  //
  // This is the interface for all interactions the upstream stream needs to have with the
  // downstream stream. It is in the GenericConnectionPoolCallbacks as the GenericConnectionPool
  // creates the GenericUpstream, and the GenericUpstream will need this interface.
  virtual UpstreamToDownstream& upstreamToDownstream() PURE;
};

/**
 * An API for sending information to either a TCP or HTTP upstream.
 *
 * It is similar logically to RequestEncoder, only without the getStream interface.
 */
class GenericUpstream {
public:
  virtual ~GenericUpstream() = default;
  /**
   * Encode a data frame.
   * @param data supplies the data to encode. The data may be moved by the encoder.
   * @param end_stream supplies whether this is the last data frame.
   */
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;
  /**
   * Encode metadata.
   * @param metadata_map_vector is the vector of metadata maps to encode.
   */
  virtual void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) PURE;
  /**
   * Encode headers, optionally indicating end of stream.
   * @param headers supplies the header map to encode.
   * @param end_stream supplies whether this is a header only request.
   */
  virtual void encodeHeaders(const Http::RequestHeaderMap& headers, bool end_stream) PURE;
  /**
   * Encode trailers. This implicitly ends the stream.
   * @param trailers supplies the trailers to encode.
   */
  virtual void encodeTrailers(const Http::RequestTrailerMap& trailers) PURE;
  /**
   * Enable/disable further data from this stream.
   */
  virtual void readDisable(bool disable) PURE;
  /**
   * Reset the stream. No events will fire beyond this point.
   * @param reason supplies the reset reason.
   */
  virtual void resetStream() PURE;
};

using GenericConnPoolPtr = std::unique_ptr<GenericConnPool>;

/*
 * A factory for creating generic connection pools.
 */
class GenericConnPoolFactory : public Envoy::Config::TypedFactory {
public:
  ~GenericConnPoolFactory() override = default;

  /*
   * @param options for creating the transport socket
   * @return may be null
   */
  virtual GenericConnPoolPtr
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const RouteEntry& route_entry,
                        absl::optional<Http::Protocol> downstream_protocol,
                        Upstream::LoadBalancerContext* ctx) const PURE;
};

using GenericConnPoolFactoryPtr = std::unique_ptr<GenericConnPoolFactory>;

} // namespace Router
} // namespace Envoy
