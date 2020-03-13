#include "common/router/retry_state_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Router {

// These are defined in envoy/router/router.h, however during certain cases the compiler is
// refusing to use the header version so allocate space here.
const uint32_t CoreRetryPolicy::RETRY_ON_5XX;
const uint32_t CoreRetryPolicy::RETRY_ON_GATEWAY_ERROR;
const uint32_t CoreRetryPolicy::RETRY_ON_CONNECT_FAILURE;
const uint32_t CoreRetryPolicy::RETRY_ON_RETRIABLE_4XX;
const uint32_t CoreRetryPolicy::RETRY_ON_RETRIABLE_HEADERS;
const uint32_t CoreRetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES;
const uint32_t CoreRetryPolicy::RETRY_ON_RESET;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_CANCELLED;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;

RetryStatePtr RetryStateImpl::create(RetryPolicy& route_policy,
                                     Http::RequestHeaderMap& request_headers,
                                     const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                                     Runtime::RandomGenerator& random,
                                     Event::Dispatcher& dispatcher,
                                     Upstream::ResourcePriority priority) {
  RetryStatePtr ret{new RetryStateImpl(route_policy, request_headers, cluster, runtime, random,
                                       dispatcher, priority)};
  return ret;
}

RetryStateImpl::RetryStateImpl(RetryPolicy& route_policy, Http::RequestHeaderMap& request_headers,
                               const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                               Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                               Upstream::ResourcePriority priority)
    : cluster_(cluster), runtime_(runtime), random_(random), dispatcher_(dispatcher),
      priority_(priority), retry_host_predicates_(route_policy.retryHostPredicates()),
      retry_priority_(route_policy.retryPriority()),
      host_selection_max_attempts_(route_policy.hostSelectionMaxAttempts()),
      retry_policy_(route_policy) {
  std::chrono::milliseconds base_interval(
      runtime_.snapshot().getInteger("upstream.base_retry_backoff_ms", 25));
  if (route_policy.baseInterval()) {
    base_interval = *route_policy.baseInterval();
  }

  // By default, cap the max interval to 10 times the base interval to ensure reasonable back-off
  // intervals.
  std::chrono::milliseconds max_interval = base_interval * 10;
  if (route_policy.maxInterval()) {
    max_interval = *route_policy.maxInterval();
  }

  backoff_strategy_ = std::make_unique<JitteredBackOffStrategy>(base_interval.count(),
                                                                max_interval.count(), random_);

  retry_policy_.recordRequestHeader(request_headers);
}

RetryStateImpl::~RetryStateImpl() { resetRetry(); }

void RetryStateImpl::enableBackoffTimer() {
  if (!retry_timer_) {
    retry_timer_ = dispatcher_.createTimer([this]() -> void { callback_(); });
  }

  // We use a fully jittered exponential backoff algorithm.
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
}

std::pair<uint32_t, bool> RetryStateImpl::parseRetryOn(absl::string_view config) {
  uint32_t ret = 0;
  bool all_fields_valid = true;
  for (const auto retry_on : StringUtil::splitToken(config, ",", false, true)) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnValues._5xx) {
      ret |= CoreRetryPolicy::RETRY_ON_5XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.GatewayError) {
      ret |= CoreRetryPolicy::RETRY_ON_GATEWAY_ERROR;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.ConnectFailure) {
      ret |= CoreRetryPolicy::RETRY_ON_CONNECT_FAILURE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Retriable4xx) {
      ret |= CoreRetryPolicy::RETRY_ON_RETRIABLE_4XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RefusedStream) {
      ret |= CoreRetryPolicy::RETRY_ON_REFUSED_STREAM;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RetriableStatusCodes) {
      ret |= CoreRetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RetriableHeaders) {
      ret |= CoreRetryPolicy::RETRY_ON_RETRIABLE_HEADERS;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Reset) {
      ret |= CoreRetryPolicy::RETRY_ON_RESET;
    } else {
      all_fields_valid = false;
    }
  }

  return {ret, all_fields_valid};
}

std::pair<uint32_t, bool> RetryStateImpl::parseRetryGrpcOn(absl::string_view retry_grpc_on_header) {
  uint32_t ret = 0;
  bool all_fields_valid = true;
  for (const auto retry_on : StringUtil::splitToken(retry_grpc_on_header, ",", false, true)) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Cancelled) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_CANCELLED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.DeadlineExceeded) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.ResourceExhausted) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Unavailable) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Internal) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_INTERNAL;
    } else {
      all_fields_valid = false;
    }
  }

  return {ret, all_fields_valid};
}

void RetryStateImpl::resetRetry() {
  if (callback_) {
    cluster_.resourceManager(priority_).retries().dec();
    callback_ = nullptr;
  }
}

RetryStatus RetryStateImpl::shouldRetry(bool would_retry, DoRetryCallback callback) {
  // If a callback is armed from a previous shouldRetry and we don't need to
  // retry this particular request, we can infer that we did a retry earlier
  // and it was successful.
  if (callback_ && !would_retry) {
    cluster_.stats().upstream_rq_retry_success_.inc();
  }

  resetRetry();

  if (!would_retry) {
    return RetryStatus::No;
  }

  if (retry_policy_.remainingRetries() == 0) {
    return RetryStatus::NoRetryLimitExceeded;
  }

  retry_policy_.remainingRetries()--;

  if (!cluster_.resourceManager(priority_).retries().canCreate()) {
    cluster_.stats().upstream_rq_retry_overflow_.inc();
    return RetryStatus::NoOverflow;
  }

  if (!runtime_.snapshot().featureEnabled("upstream.use_retry", 100)) {
    return RetryStatus::No;
  }

  ASSERT(!callback_);
  callback_ = callback;
  cluster_.resourceManager(priority_).retries().inc();
  cluster_.stats().upstream_rq_retry_.inc();
  enableBackoffTimer();
  return RetryStatus::Yes;
}

RetryStatus RetryStateImpl::shouldRetryHeaders(const Http::ResponseHeaderMap& response_headers,
                                               DoRetryCallback callback) {
  retry_policy_.recordResponseHeaders(response_headers);
  return shouldRetry(wouldRetryFromHeaders(response_headers), callback);
}

RetryStatus RetryStateImpl::shouldRetryReset(Http::StreamResetReason reset_reason,
                                             DoRetryCallback callback) {
  retry_policy_.recordReset(reset_reason);
  return shouldRetry(wouldRetryFromReset(reset_reason), callback);
}

RetryStatus RetryStateImpl::shouldHedgeRetryPerTryTimeout(DoRetryCallback callback) {
  // A hedged retry on per try timeout is always retried if there are retries
  // left. NOTE: this is a bit different than non-hedged per try timeouts which
  // are only retried if the applicable retry policy specifies either
  // RETRY_ON_5XX or RETRY_ON_GATEWAY_ERROR. This is because these types of
  // retries are associated with a stream reset which is analogous to a gateway
  // error. When hedging on per try timeout is enabled, however, there is no
  // stream reset.
  return shouldRetry(true, callback);
}

bool RetryStateImpl::wouldRetryFromHeaders(const Http::ResponseHeaderMap& response_headers) {
  return retry_policy_.wouldRetryFromHeaders(response_headers);
}

bool RetryStateImpl::wouldRetryFromReset(const Http::StreamResetReason reset_reason) {
  return retry_policy_.wouldRetryFromReset(reset_reason);
}

} // namespace Router
} // namespace Envoy
