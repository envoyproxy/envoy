#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

#include "absl/types/span.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class CommonFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Router {

/**
 * An interface to be implemented by rate limited reset header parsers.
 */
class ResetHeaderParser {
public:
  virtual ~ResetHeaderParser() = default;

  /**
   * Iterate over the headers, choose the first one that matches by name, and try to parse its
   * value.
   */
  virtual std::optional<std::chrono::milliseconds>
  parseInterval(TimeSource& time_source, const Http::HeaderMap& headers) const PURE;
};

using ResetHeaderParserSharedPtr = std::shared_ptr<ResetHeaderParser>;

/**
 * RetryStatus whether request should be retried or not.
 */
enum class RetryStatus {
  No,
  NoOverflow,
  NoRetryLimitExceeded,
  Yes,
  NoRuntime,
};

/**
 * Wraps retry state for an active routed request.
 */
class RetryState {
public:
  enum class RetryDecision {
    // Retry the request immediately.
    RetryImmediately,
    // Retry the request with timed backoff delay.
    RetryWithBackoff,
    // Do not retry.
    NoRetry,
  };

  enum class Http3Used {
    Unknown,
    Yes,
    No,
  };

  enum class DoRetryType {
    // Retry the request immediately.
    Immediately,
    // Retry the request with ratelimited backoff delay.
    Ratelimited,
    // Retry the request with exponential backoff delay.
    Exponential,
  };

  using DoRetryCallback = std::function<void()>;
  /**
   * @param disabled_http3 indicates whether the retry should disable http3 or not.
   */
  using DoRetryResetCallback = std::function<void(bool disable_http3)>;
  /**
   * @param disable_early_data indicates whether the retry should disable early data or not.
   */
  using DoRetryHeaderCallback = std::function<void(bool disable_early_data)>;

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
  virtual std::optional<std::chrono::milliseconds>
  parseResetInterval(const Http::ResponseHeaderMap& response_headers) const PURE;

  /**
   * Determine whether a request should be retried based on the response headers.
   * @param response_headers supplies the response headers.
   * @param original_request supplies the original request headers.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   *                 inline.
   * @return RetryStatus if a retry should take place. @param callback will be called at some point
   *         in the future. Otherwise a retry should not take place and the callback will never be
   *         called. Calling code should proceed with error handling.
   */
  virtual RetryStatus shouldRetryHeaders(const Http::ResponseHeaderMap& response_headers,
                                         const Http::RequestHeaderMap& original_request,
                                         DoRetryHeaderCallback callback) PURE;

  /**
   * Determines whether given response headers would be retried by the retry policy, assuming
   * sufficient retry budget and circuit breaker headroom. This is useful in cases where
   * the information about whether a response is "good" or not is useful, but a retry should
   * not be attempted for other reasons.
   * @param response_headers supplies the response headers.
   * @param original_request supplies the original request headers.
   * @param retry_as_early_data output argument to tell the caller if a retry should be sent as
   *        early data if it is warranted.
   * @return RetryDecision if a retry would be warranted based on the retry policy and if it would
   *         be warranted with timed backoff.
   */
  virtual RetryDecision wouldRetryFromHeaders(const Http::ResponseHeaderMap& response_headers,
                                              const Http::RequestHeaderMap& original_request,
                                              bool& retry_as_early_data) PURE;

  /**
   * Determine whether a request should be retried after a reset based on the reason for the reset.
   * @param reset_reason supplies the reset reason.
   * @param http3_used whether the reset request was sent over http3 as alternate protocol or
   *                   not. nullopt means it wasn't sent at all before getting reset.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   *                 inline.
   * @param upstream_request_started indicates whether the first byte has been transmitted to the
   *                                 upstream server.
   *
   * @return RetryStatus if a retry should take place. @param callback will be called at some point
   *         in the future. Otherwise a retry should not take place and the callback will never be
   *         called. Calling code should proceed with error handling.
   */
  virtual RetryStatus shouldRetryReset(Http::StreamResetReason reset_reason, Http3Used http3_used,
                                       DoRetryResetCallback callback,
                                       bool upstream_request_started) PURE;

  /**
   * Determine whether a "hedged" retry should be sent after the per try
   * timeout expires. This means the original request is not canceled, but a
   * new one is sent to hedge against the original request taking even longer.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   * inline.
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
   * @param stream_info request stream information.
   * @param priority_set current priority set.
   * @param original_priority_load original priority load.
   * @param priority_mapping_func see @Upstream::RetryPriority::PriorityMappingFunc.
   * @return HealthyAndDegradedLoad that should be used to select a priority for the next retry.
   */
  virtual const Upstream::HealthyAndDegradedLoad& priorityLoadForRetry(
      StreamInfo::StreamInfo* stream_info, const Upstream::PrioritySet& priority_set,
      const Upstream::HealthyAndDegradedLoad& original_priority_load,
      const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) PURE;
  /**
   * return how many times host selection should be reattempted during host selection.
   */
  virtual uint32_t hostSelectionMaxAttempts() const PURE;

  /**
   * @return the type of retry to perform when a retry is triggered.
   * This is used to determine whether to apply a backoff delay or not, and if so, what type of
   * backoff to apply.
   */
  virtual DoRetryType doRetryType() const PURE;

  /**
   * @return true if this retry state was automatically configured for HTTP/3.
   */
  virtual bool isAutomaticallyConfiguredForHttp3() const PURE;
};

using RetryStatePtr = std::unique_ptr<RetryState>;

/**
 * Route level retry policy.
 */
class RetryPolicy {
public:
  // clang-format off
  static constexpr uint32_t RETRY_ON_5XX                                = 0x1;
  static constexpr uint32_t RETRY_ON_GATEWAY_ERROR                      = 0x2;
  static constexpr uint32_t RETRY_ON_CONNECT_FAILURE                    = 0x4;
  static constexpr uint32_t RETRY_ON_RETRIABLE_4XX                      = 0x8;
  static constexpr uint32_t RETRY_ON_REFUSED_STREAM                     = 0x10;
  static constexpr uint32_t RETRY_ON_GRPC_CANCELLED                     = 0x20;
  static constexpr uint32_t RETRY_ON_GRPC_DEADLINE_EXCEEDED             = 0x40;
  static constexpr uint32_t RETRY_ON_GRPC_RESOURCE_EXHAUSTED            = 0x80;
  static constexpr uint32_t RETRY_ON_GRPC_UNAVAILABLE                   = 0x100;
  static constexpr uint32_t RETRY_ON_GRPC_INTERNAL                      = 0x200;
  static constexpr uint32_t RETRY_ON_RETRIABLE_STATUS_CODES             = 0x400;
  static constexpr uint32_t RETRY_ON_RESET                              = 0x800;
  static constexpr uint32_t RETRY_ON_RETRIABLE_HEADERS                  = 0x1000;
  static constexpr uint32_t RETRY_ON_ENVOY_RATE_LIMITED                 = 0x2000;
  static constexpr uint32_t RETRY_ON_HTTP3_POST_CONNECT_FAILURE         = 0x4000;
  static constexpr uint32_t RETRY_ON_RESET_BEFORE_REQUEST               = 0x8000;
  // clang-format on

  virtual ~RetryPolicy() = default;

  /**
   * @return std::chrono::milliseconds timeout per retry attempt. 0 is disabled.
   */
  virtual std::chrono::milliseconds perTryTimeout() const PURE;

  /**
   * @return std::chrono::milliseconds the optional per try idle timeout. 0 is disabled.
   */
  virtual std::chrono::milliseconds perTryIdleTimeout() const PURE;

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
   * @return the retry options predicates for this policy. Each policy will be applied prior
   * to retrying a request, allowing for request behavior to be customized.
   */
  virtual absl::Span<const Upstream::RetryOptionsPredicateConstSharedPtr>
  retryOptionsPredicates() const PURE;

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
   * @return std::optional<std::chrono::milliseconds> base retry interval
   */
  virtual std::optional<std::chrono::milliseconds> baseInterval() const PURE;

  /**
   * @return std::optional<std::chrono::milliseconds> maximum retry interval
   */
  virtual std::optional<std::chrono::milliseconds> maxInterval() const PURE;

  /**
   * @return std::vector<Http::ResetHeaderParserSharedPtr>& list of reset header
   * parsers that will be used to extract a retry back-off interval from response headers.
   */
  virtual const std::vector<ResetHeaderParserSharedPtr>& resetHeaders() const PURE;

  /**
   * @return std::chrono::milliseconds upper limit placed on a retry
   * back-off interval parsed from response headers.
   */
  virtual std::chrono::milliseconds resetMaxInterval() const PURE;

  /**
   * @return whether the route-selected upstream cluster should be refreshed before a retry attempt.
   */
  virtual bool refreshClusterOnRetry() const PURE;

  /**
   * Creates the RetryState to be used for a request with this retry policy.
   * @param request_headers the request headers.
   * @param cluster the upstream cluster info.
   * @param context the factory context.
   * @param dispatcher the event dispatcher.
   * @param priority the routing priority.
   * @param stream_info optional stream info for the request.
   * @return the RetryState to use, or nullptr if retries are disabled for this request.
   */
  virtual RetryStatePtr createRetryState(Http::RequestHeaderMap& request_headers,
                                         const Upstream::ClusterInfo& cluster,
                                         Server::Configuration::CommonFactoryContext& context,
                                         Event::Dispatcher& dispatcher,
                                         Upstream::ResourcePriority priority,
                                         OptRef<StreamInfo::StreamInfo> stream_info) const PURE;
};

using RetryPolicyConstSharedPtr = std::shared_ptr<const RetryPolicy>;

} // namespace Router
} // namespace Envoy
