#include "common/router/retry_state_impl.h"

#include "envoy/config/route/v3/route_components.pb.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Router {

RetryStatePtr RetryStateImpl::create(RetryPolicy& route_policy,
                                     Http::RequestHeaderMap& request_headers,
                                     const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                                     Runtime::RandomGenerator& random,
                                     Event::Dispatcher& dispatcher,
                                     Upstream::ResourcePriority priority) {
  RetryStatePtr ret;
  // We short circuit here and do not bother with an allocation if there is no chance we will retry.
  if (request_headers.EnvoyRetryOn() || request_headers.EnvoyRetryGrpcOn() ||
      route_policy.enabled()) {
    ret.reset(new RetryStateImpl(route_policy, request_headers, cluster, runtime, random,
                                 dispatcher, priority));
  }

  request_headers.removeEnvoyRetryOn();
  request_headers.removeEnvoyRetryGrpcOn();
  request_headers.removeEnvoyMaxRetries();
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
  std::cout << "Before:" << retry_policy_.enabled() << std::endl;
  // Inject request headers to retry policy.
  retry_policy_.recordRequestHeader(request_headers);
  std::cout << "After:" << retry_policy_.enabled() << std::endl;

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
}

RetryStateImpl::~RetryStateImpl() { resetRetry(); }

void RetryStateImpl::enableBackoffTimer() {
  if (!retry_timer_) {
    retry_timer_ = dispatcher_.createTimer([this]() -> void { callback_(); });
  }

  // We use a fully jittered exponential backoff algorithm.
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
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
